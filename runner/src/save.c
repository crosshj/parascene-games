#include "save.h"

#include <psxapi.h>
#include <stdint.h>
#include <string.h>

/* One block. The icon header is the part the memory-card browser shows.
 * The record sits at 0x200, on a sector boundary:
 *   0x200 magic "UUUV"
 *   0x204 version
 *   0x205 record size
 *   0x206 checksum, 16-bit, little-endian, over the record
 *   0x208 record. Byte 0 counts unlocked story stages in world order.
 *   One past the last stage means that stage has been cleared.
 * A write covers the header and the record, 0x280 bytes, five sectors. */
#define SAVE_PATH "bu00:UUUVVV-RUNNER"
#define SAVE_BYTES 0x280
#define SAVE_MAGIC_AT 0x200
#define SAVE_RECORD 1

#define CARD_PORT 0
/* The BIOS accepts read or write, not both, in the low bits. */
#define MODE_READ 1
#define MODE_WRITE 2
#define MODE_CREATE (1 << 9)
#define MODE_BLOCKS(n) ((n) << 16)

enum {
    SV_BOOT,
    SV_INFO,
    SV_CLEAR,
    SV_DIR,
    SV_WAIT,
    SV_LOAD,
    SV_READY
};

static uint8_t buf[SAVE_BYTES] __attribute__((aligned(4)));
static int state = SV_BOOT;
static int read_tries;
static int has_file;
static int write_lock;
static int want_write;
static int write_cool;
static int pending = 1;
static int loaded;
static int generation;
#ifndef RUNNER_WEB
static int ev_sw[4];
static int ev_hw[4];
static int wait_left;
static int new_tries;
#endif

static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

#ifndef RUNNER_WEB
static void events_eat(void) {
    int i;
    for (i = 0; i < 4; i++) {
        TestEvent(ev_sw[i]);
        TestEvent(ev_hw[i]);
    }
}

/* 1 finished, 2 a card was just inserted, 0 failed, -1 still waiting. */
static int events_poll(void) {
    if (TestEvent(ev_sw[0]) || TestEvent(ev_hw[0]))
        return 1;
    if (TestEvent(ev_sw[3]) || TestEvent(ev_hw[3]))
        return 2;
    if (TestEvent(ev_sw[1]) || TestEvent(ev_hw[1]))
        return 0;
    if (TestEvent(ev_sw[2]) || TestEvent(ev_hw[2]))
        return 0;
    return -1;
}

static int wait_result(void) {
    int r = events_poll();
    if (r >= 0)
        return r;
    if (--wait_left > 0)
        return -1;
    /* Some BIOS paths finish inside the call and leave no event. */
    if (_card_status(CARD_PORT) == 1)
        return 1;
    return 0;
}

static void open_events(void) {
    static const uint32_t spec[4] = { EvSpIOE, EvSpERROR, EvSpTIMOUT, EvSpNEW };
    int i;
    for (i = 0; i < 4; i++) {
        ev_sw[i] = OpenEvent(SwCARD, spec[i], EvMdNOINTR, 0);
        ev_hw[i] = OpenEvent(HwCARD, spec[i], EvMdNOINTR, 0);
        EnableEvent(ev_sw[i]);
        EnableEvent(ev_hw[i]);
    }
}

static void kick(int next, int frames) {
    events_eat();
    wait_left = frames;
    state = next;
}
#endif

/* "UUUVVV RUNNER" as fullwidth Shift-JIS, the encoding the card browser uses. */
static void write_title(uint8_t *p) {
    static const char text[] = "UUUVVV RUNNER";
    int i;
    for (i = 0; text[i]; i++) {
        char c = text[i];
        if (c == ' ') {
            p[0] = 0x81;
            p[1] = 0x40;
        } else if (c >= 'A' && c <= 'Z') {
            p[0] = 0x82;
            p[1] = (uint8_t)(0x60 + (c - 'A'));
        }
        p += 2;
    }
}

static void fill_buf(int reach) {
    int sum = reach;
    memset(buf, 0, sizeof buf);
    buf[0] = 'S';
    buf[1] = 'C';
    buf[2] = 0x11;
    buf[3] = 1;
    write_title(buf + 4);
    /* Color 1 is the solid icon. Color 0 stays transparent. */
    put16(buf + 0x62, 0x3A46);
    memset(buf + 0x80, 0x11, 128);
    buf[SAVE_MAGIC_AT] = 'U';
    buf[SAVE_MAGIC_AT + 1] = 'U';
    buf[SAVE_MAGIC_AT + 2] = 'U';
    buf[SAVE_MAGIC_AT + 3] = 'V';
    buf[0x204] = SAVE_VERSION;
    buf[0x205] = SAVE_RECORD;
    buf[0x208] = (uint8_t)reach;
    put16(buf + 0x206, (uint16_t)sum);
}

static void parse_buf(void) {
    const uint8_t *rec;
    int size;
    int i;
    int sum = 0;
    loaded = 0;
    write_lock = 0;
    if (buf[SAVE_MAGIC_AT] != 'U' || buf[SAVE_MAGIC_AT + 1] != 'U' ||
        buf[SAVE_MAGIC_AT + 2] != 'U' || buf[SAVE_MAGIC_AT + 3] != 'V')
        return;
    if (buf[0x204] > SAVE_VERSION) {
        write_lock = 1;
        return;
    }
    if (buf[0x204] != SAVE_VERSION)
        return;
    size = buf[0x205];
    if (size != SAVE_RECORD)
        return;
    rec = buf + 0x208;
    for (i = 0; i < size; i++)
        sum = (sum + rec[i]) & 0xFFFF;
    if (buf[0x206] != (sum & 0xFF) || buf[0x207] != ((sum >> 8) & 0xFF))
        return;
    if (rec[0] >= 1)
        loaded = rec[0];
}

/* The browser BIOS stand-in only serves a card that already has an "MC"
 * header. A fresh image is all zeroes, which the file calls treat as no
 * card. Write the blank header the stand-in itself uses. */
#ifdef RUNNER_WEB
static void ensure_header(void) {
    static int checked;
    uint8_t sec[128];
    int s;
    if (checked)
        return;
    checked = 1;
    _card_read(CARD_PORT, 0, sec);
    if (sec[0] == 'M' && sec[1] == 'C')
        return;
    memset(sec, 0, sizeof sec);
    sec[0] = 'M';
    sec[1] = 'C';
    sec[127] = 0x0e;
    _card_write(CARD_PORT, 0, sec);
    memset(sec, 0, sizeof sec);
    sec[0] = 0xa0;
    sec[8] = 0xff;
    sec[9] = 0xff;
    sec[127] = 0xa0;
    for (s = 1; s <= 15; s++)
        _card_write(CARD_PORT, s, sec);
}
#endif

static void do_read(void) {
    int fd;
    int n;
#ifdef RUNNER_WEB
    ensure_header();
#endif
    fd = open(SAVE_PATH, MODE_READ);
    if (fd < 0) {
#ifdef RUNNER_WEB
        /* A missing file and a card that is still waking up look the same
         * for a moment. An existing file opens, so this wait is only the
         * empty-card case. */
        if (++read_tries < 20)
            return;
#endif
        has_file = 0;
        loaded = 0;
        state = SV_READY;
        return;
    }
    n = read(fd, buf, SAVE_BYTES);
    close(fd);
    if (n != SAVE_BYTES) {
        if (++read_tries < 60)
            return;
        has_file = 1;
        loaded = 0;
        write_lock = 1;
        state = SV_READY;
        return;
    }
    has_file = 1;
    parse_buf();
    state = SV_READY;
}

static void do_write(void) {
    int fd;
    int n;
    int mode;
    if (write_cool > 0) {
        write_cool--;
        return;
    }
    /* A load that arrived after an earlier unlock wins when it is ahead. */
    if (pending < loaded)
        pending = loaded;
    if (has_file && pending == loaded) {
        want_write = 0;
        return;
    }
    fill_buf(pending);
#ifndef RUNNER_WEB
    events_eat();
#endif
    mode = MODE_WRITE;
    if (!has_file)
        mode |= MODE_CREATE | MODE_BLOCKS(1);
    fd = open(SAVE_PATH, mode);
    if (fd < 0)
        fd = open(SAVE_PATH, has_file ? MODE_WRITE | MODE_CREATE | MODE_BLOCKS(1) : MODE_WRITE);
    if (fd < 0) {
        write_cool = 16;
        return;
    }
    n = write(fd, buf, SAVE_BYTES);
    close(fd);
    if (n != SAVE_BYTES) {
        write_cool = 16;
        return;
    }
    has_file = 1;
    loaded = pending;
    want_write = 0;
    generation = generation >= 255 ? 1 : generation + 1;
}

void save_init(void) {
    int was = EnterCriticalSection();
    InitCARD(1);
    StartCARD();
    _bu_init();
    if (was)
        ExitCriticalSection();
#ifdef RUNNER_WEB
    /* No BIOS events. Slot 1 is the image the page saves. */
    state = SV_LOAD;
#else
    open_events();
    state = SV_BOOT;
#endif
}

void save_store(int reach) {
    if (write_lock)
        return;
    if (reach < 1)
        reach = 1;
    if (reach > 255)
        reach = 255;
    pending = reach;
    write_cool = 0;
    want_write = 1;
}

int save_generation(void) {
    return generation;
}

int save_poll(int *reach) {
#ifdef RUNNER_WEB
    if (state == SV_LOAD)
        do_read();
    else if (state == SV_READY && want_write && !write_lock)
        do_write();
#else
    int r;
    /* The pad driver shares this bus. The probe starts on the first poll,
     * which is after ChangeClearPAD(0). Starting it inside save_init
     * cancels the transfer and the card never answers. A miss used to
     * stick: the card was never asked again. */
    if (state == SV_BOOT) {
        kick(SV_INFO, 180);
        _card_info(CARD_PORT);
    } else if (state == SV_INFO) {
        r = wait_result();
        if (r == 2 && new_tries < 3) {
            new_tries++;
            kick(SV_CLEAR, 120);
            _card_clear(CARD_PORT);
        } else if (r == 1) {
            new_tries = 0;
            kick(SV_DIR, 240);
            _card_load(CARD_PORT);
        } else if (r == 0 || r == 2) {
            new_tries = 0;
            kick(SV_WAIT, 90);
        }
    } else if (state == SV_CLEAR) {
        r = wait_result();
        if (r == 1) {
            kick(SV_INFO, 180);
            _card_info(CARD_PORT);
        } else if (r == 0 || r == 2)
            kick(SV_WAIT, 90);
    } else if (state == SV_DIR) {
        r = wait_result();
        if (r == 1)
            state = SV_LOAD;
        else if (r == 0 || r == 2)
            kick(SV_WAIT, 90);
    } else if (state == SV_WAIT) {
        if (--wait_left <= 0) {
            kick(SV_INFO, 180);
            _card_info(CARD_PORT);
        }
    } else if (state == SV_LOAD) {
        do_read();
    } else if (state == SV_READY && want_write && !write_lock) {
        do_write();
    }
#endif
    if (reach)
        *reach = state == SV_READY ? loaded : 0;
    return state == SV_READY;
}

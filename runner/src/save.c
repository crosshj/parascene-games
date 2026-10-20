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
 *   0x208 record. Byte 0 is how many story stages are unlocked.
 * A write covers the header and the record, 0x280 bytes, five sectors. */
#define SAVE_PATH "bu00:UUUVVV-RUNNER"
#define SAVE_BYTES 0x280
#define SAVE_MAGIC_AT 0x200
#define SAVE_RECORD 1

#define CARD_PORT 0
#define MODE_READ 1
#define MODE_WRITE 2
#define MODE_CREATE (1 << 9)
#define MODE_BLOCKS(n) ((n) << 16)

enum {
    SV_INFO,
    SV_CLEAR,
    SV_DIR,
    SV_LOAD,
    SV_READY,
    SV_NONE
};

static uint8_t buf[SAVE_BYTES] __attribute__((aligned(4)));
static int ev_sw[4];
static int ev_hw[4];
static int state = SV_NONE;
static int wait_left;
static int new_tries;
static int has_file;
static int write_lock;
static int want_write;
static int write_tries;
static int pending = 1;
static int loaded;
static int generation;

static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

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

static int open_file(int create) {
    int mode = MODE_READ | MODE_WRITE;
    if (create)
        mode |= MODE_CREATE | MODE_BLOCKS(1);
    return open(SAVE_PATH, mode);
}

static void do_read(void) {
    int fd = open(SAVE_PATH, MODE_READ);
    int n;
    if (fd < 0) {
        has_file = 0;
        loaded = 0;
        state = SV_READY;
        return;
    }
    n = read(fd, buf, SAVE_BYTES);
    close(fd);
    has_file = 1;
    if (n == SAVE_BYTES)
        parse_buf();
    else
        loaded = 0;
    state = SV_READY;
}

static void do_write(void) {
    int fd;
    int n;
    fill_buf(pending);
    events_eat();
    fd = open_file(!has_file);
    if (fd < 0)
        fd = open_file(has_file);
    if (fd < 0) {
        if (++write_tries >= 4)
            want_write = 0;
        return;
    }
    n = write(fd, buf, SAVE_BYTES);
    close(fd);
    if (n != SAVE_BYTES) {
        if (++write_tries >= 4)
            want_write = 0;
        return;
    }
    has_file = 1;
    write_tries = 0;
    want_write = 0;
    generation = generation >= 255 ? 1 : generation + 1;
}

static void kick(int next, int frames) {
    events_eat();
    wait_left = frames;
    state = next;
}

void save_init(void) {
    InitCARD(1);
    StartCARD();
    _bu_init();
    open_events();
    kick(SV_INFO, 120);
    _card_info(CARD_PORT);
}

void save_store(int reach) {
    if (write_lock)
        return;
    if (reach < 1)
        reach = 1;
    if (reach > 255)
        reach = 255;
    pending = reach;
    write_tries = 0;
    want_write = 1;
}

int save_generation(void) {
    return generation;
}

int save_poll(int *reach) {
    int r;
    if (state == SV_INFO) {
        r = wait_result();
        if (r == 2 && new_tries < 3) {
            new_tries++;
            kick(SV_CLEAR, 120);
            _card_clear(CARD_PORT);
        } else if (r == 1) {
            kick(SV_DIR, 240);
            _card_load(CARD_PORT);
        } else if (r == 0 || r == 2) {
            state = SV_NONE;
        }
    } else if (state == SV_CLEAR) {
        r = wait_result();
        if (r >= 0) {
            kick(SV_INFO, 120);
            _card_info(CARD_PORT);
        }
    } else if (state == SV_DIR) {
        r = wait_result();
        if (r == 1)
            state = SV_LOAD;
        else if (r == 0)
            state = SV_NONE;
    } else if (state == SV_LOAD) {
        do_read();
    } else if (state == SV_READY && want_write && !write_lock) {
        do_write();
    }
    if (reach)
        *reach = state == SV_READY ? loaded : 0;
    return state == SV_READY || state == SV_NONE;
}

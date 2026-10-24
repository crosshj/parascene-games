/* Pale Meridian: an ice crack under an open sky. There is no ridge. The
 * floor meets the wall, and the wall sheets meet each other, so the crack
 * is closed. The walls grow where the road has dropped, and stay low on
 * the lip. Curtains of aurora hang in screen space above the crack. A
 * fork still clips the floor short of the other road. Stage 2 pulls a
 * green haze across the ice. Stage 3 lets it go dark. */

#include "world.h"
#include "course.h"
#include "path.h"
#include "road.h"
#include "level.h"

#define EDGE_END 2200
#define SHOULDER_W 40
#define WALL_OUT 108
/* Independent road/wall sampling must overlap at the wall's foot. */
#define GROUND_W (WALL_OUT + 32)

typedef struct {
    uint8_t sky[3], haze[3], tint[3], shadow[3], top[3];
    int density;
    uint8_t ridge_far[3], ridge_near[3];
    uint8_t asphalt[3], asphalt_far[3], curb[3], dash[3];
    uint8_t shoulder_c[3], ground_c[3];
} Look;

static const ObstacleStyle k_obstacle = {
    {140, 170, 180}, {160, 190, 200}, {180, 210, 216}, {230, 245, 250}, {200, 230, 255}
};

static const Look k_look[3] = {
    {
        {176, 214, 224}, {196, 224, 230}, {150, 168, 176}, {90, 120, 140}, {96, 160, 196},
        20,
        {0, 0, 0}, {0, 0, 0},
        {186, 214, 222}, {200, 222, 228}, {246, 252, 255}, {226, 238, 242},
        {206, 226, 232}, {170, 206, 216}
    },
    {
        {36, 78, 132}, {80, 176, 156}, {120, 160, 156}, {40, 70, 90}, {12, 28, 64},
        52,
        {0, 0, 0}, {0, 0, 0},
        {120, 170, 176}, {90, 160, 156}, {230, 255, 246}, {200, 230, 220},
        {140, 190, 186}, {70, 140, 150}
    },
    {
        {14, 28, 52}, {28, 48, 68}, {110, 130, 150}, {24, 36, 56}, {4, 10, 24},
        112,
        {0, 0, 0}, {0, 0, 0},
        {48, 72, 90}, {24, 40, 56}, {210, 230, 240}, {160, 180, 190},
        {36, 56, 72}, {16, 28, 44}
    },
};

/* Ground-reflected fill: strongest in the two bright stages, none at night. */
static const uint8_t k_runner_ambient[3][3] = {
    {40,52,60},
    {28,44,44},
    {0,0,0}
};

static Light live;
static RoadStyle road;
static uint8_t sky_top[3];
static int shown = -1;

static void rgb(uint8_t *d, const uint8_t *s) {
    d[0] = s[0];
    d[1] = s[1];
    d[2] = s[2];
}

static void apply(int stage) {
    const Look *L;
    if (stage < 0)
        stage = 0;
    if (stage > 2)
        stage = 2;
    if (stage == shown)
        return;
    shown = stage;
    L = &k_look[stage];
    rgb(live.sky, L->sky);
    rgb(live.haze, L->haze);
    rgb(live.tint, L->tint);
    rgb(live.shadow, L->shadow);
    rgb(live.runner_ambient,k_runner_ambient[stage]);
    live.density = L->density;
    rgb(sky_top, L->top);
    road.light = &live;
    road.obstacle = &k_obstacle;
    road.edge_end = EDGE_END;
    road.shoulder = SHOULDER_W;
    road.ground = GROUND_W;
    road.ground_underlay = 1;
    rgb(road.ridge_far, L->ridge_far);
    rgb(road.ridge_near, L->ridge_near);
    rgb(road.road, L->asphalt);
    rgb(road.road_far, L->asphalt_far);
    rgb(road.edge, L->curb);
    rgb(road.dash, L->dash);
    rgb(road.shoulder_c, L->shoulder_c);
    rgb(road.ground_c, L->ground_c);
}

static void meridian_clear(const LevelFrame *f, int *r, int *g, int *b) {
    apply(f->stage);
    *r = live.sky[0];
    *g = live.sky[1];
    *b = live.sky[2];
}

static int wrap_shift(int yaw, int denom) {
    int s = yaw * SCREEN_W / denom;
    s %= SCREEN_W;
    if (s < 0)
        s += SCREEN_W;
    return s;
}

static void curtain(int x, int w, int top, int foot, int r, int g, int b) {
    XY p0, p1, p2, p3;
    if (w < 2 || top >= foot || x >= SCREEN_W || x + w <= 0)
        return;
    p0.x = (int16_t)(x + 1);
    p0.y = (int16_t)top;
    p1.x = (int16_t)(x + w - 1);
    p1.y = (int16_t)top;
    /* The foot leans, so the veil hangs instead of standing. */
    p2.x = (int16_t)(x + 5);
    p2.y = (int16_t)foot;
    p3.x = (int16_t)(x + w + 5);
    p3.y = (int16_t)foot;
    /* Averaged with the sky, so the curtain stays light, not a tower. */
    poly_f4(&p0, &p1, &p2, &p3, OT_LENGTH - 2, r, g, b, BLEND_AVG);
}

/* Four curtains in the sky above the crack. They stay over the road,
 * where the walls do not cover them. Screen space, slid a little by the
 * heading. The middle stage is the green one. The last stage is dimmer. */
static void aurora(int stage) {
    static const int k_x[4] = { 78, 124, 176, 222 };
    int foot = path_horizon();
    int shift = wrap_shift(path_yaw(), 4096);
    int i;
    int r = stage == 2 ? 20 : 50;
    int g = stage == 2 ? 90 : stage == 1 ? 195 : 175;
    int b = stage == 2 ? 70 : 155;
    if (world_bytes_used() + 8 * 1024 > BUFFER_LENGTH)
        return;
    if (foot < 28)
        return;
    if (foot > 120)
        foot = 120;
    for (i = 0; i < 4; i++) {
        int w = 11 + (i & 1) * 4;
        int k;
        for (k = -1; k <= 0; k++) {
            int x = k_x[i] - shift + k * SCREEN_W;
            curtain(x, w, 2, foot - 4, r, g, b);
            curtain(x + w / 4, w / 2, 2, foot / 2, r + 30, g, b + 20);
        }
    }
}

static int crack_h(int s, int lip) {
    const PathSeg *seg = path_at(s);
    int drop, h;
    if (!seg)
        return 220;
    drop = lip - seg->y;
    if (drop < 0)
        drop = 0;
    h = 220 + drop / 2;
    if (h > 960)
        h = 960;
    return h;
}

static void ice(const LevelFrame *f) {
    const PathSeg *lip = path_at(0);
    int top = lip ? lip->y : 0;
    int step = 512;
    int reach = ROAD_HALF + WALL_OUT;
    int s = f->scroll / step * step;
    int s1, draw, compiled_start=road_compiled_walls_start(f->scroll);
    for (; (draw = road_span(s, f->scroll, step, reach, 960, &s1)) != 0 &&
           (compiled_start<0 || s<compiled_start); s = s1) {
        int h, r, g, b;
        if(compiled_start>=0 && s1>compiled_start) s1=compiled_start;
        if (draw == 2)
            continue;
        h = crack_h((s + s1) / 2, top);
        r = road.shoulder_c[0];
        g = road.shoulder_c[1];
        b = road.shoulder_c[2];
        if (!road_fork_cloth(s, s1, -1, WALL_OUT, 0, WALL_OUT, h, r, g, b))
            return;
        if (!road_fork_cloth(s, s1, 1, WALL_OUT, 0, WALL_OUT, h, r, g, b))
            return;
    }
    if(compiled_start>=0)
        road_compiled_walls(f->scroll,compiled_start,road.shoulder_c[0],
                            road.shoulder_c[1],road.shoulder_c[2]);

    /* The baked boundary contains both physical branches and the closed
     * inner island in world coordinates. Render it once from this camera. */
    road_compiled_fork_walls(f->scroll,road.shoulder_c[0],
                            road.shoulder_c[1],road.shoulder_c[2]);
}

static void meridian_draw(const LevelFrame *f) {
    int hy;
    apply(f->stage);
    hy = path_horizon();
    if (hy < 8)
        hy = 8;
    if (hy > SCREEN_H - 8)
        hy = SCREEN_H - 8;
    gradient(0, 0, SCREEN_W, hy, OT_LENGTH - 1,
             sky_top[0], sky_top[1], sky_top[2],
             live.sky[0], live.sky[1], live.sky[2]);
    aurora(f->stage);
    road_draw(&road, f);
    ice(f);
}

static const Stage k_stages[] = {
    {"THE DRIFT", "meridian.drift", 0, STAGE_FINISH},
    {"THE FORK", "meridian.fork", 0, STAGE_FINISH},
    {"THE VERGE", "meridian.verge", 0, STAGE_FINISH},
};

const Level level_meridian = {
    "PALE MERIDIAN",
    "meridian.drift",
    meridian_clear,
    meridian_draw,
    &live,
    1,
    k_stages,
    3,
};

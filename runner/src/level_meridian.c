/* Pale Meridian: an ice crack under an open sky. There is no ridge. The
 * lip is short and the walls stand just outside it, so the floor never
 * runs off into a sheet that could cover a fork. The walls grow where the
 * road has dropped, and stay low on the lip. Stage 2 pulls a green haze
 * across the ice. Stage 3 lets it go dark. */

#include "world.h"
#include "course.h"
#include "path.h"
#include "road.h"
#include "level.h"

#define EDGE_END 2200
#define SHOULDER_W 40
#define GROUND_W 96

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
    live.density = L->density;
    rgb(sky_top, L->top);
    road.light = &live;
    road.obstacle = &k_obstacle;
    road.edge_end = EDGE_END;
    road.shoulder = SHOULDER_W;
    road.ground = GROUND_W;
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
    int s, base = f->scroll / 768 * 768;
    for (s = base; s < f->scroll + 6400; s += 768) {
        int h = crack_h(s + 384, top);
        int r = road.shoulder_c[0];
        int g = road.shoulder_c[1];
        int b = road.shoulder_c[2];
        if (!road_cloth(s, s + 700, -1, 108, 0, 108, h, r, g, b))
            return;
        if (!road_cloth(s, s + 700, 1, 108, 0, 108, h, r, g, b))
            return;
    }
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
    road_draw(&road, f);
    ice(f);
    road_streaks(f, road.edge[0], road.edge[1], road.edge[2], 200, 0);
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

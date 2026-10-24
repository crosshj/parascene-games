/* The Sineworks: the inside of a machine. There is no ridge and nothing
 * outside the tube: the walls and the ceiling are the world, and the lip
 * stops at the wall. The clear color is the far end of the tunnel. The
 * air thickens each stage. */

#include "world.h"
#include "course.h"
#include "path.h"
#include "road.h"
#include "level.h"
#include "world_mesh.h"
#include "profile.h"

#define EDGE_END 2400
#define SHOULDER_W 16
#define TUBE_OUT 36
#define GROUND_W TUBE_OUT
/* Only used by the procedural fallback. */
#define DETAIL_Z 2200

typedef struct {
    uint8_t sky[3], haze[3], tint[3], shadow[3], top[3];
    int density;
    uint8_t ridge_far[3], ridge_near[3];
    uint8_t asphalt[3], asphalt_far[3], curb[3], dash[3];
    uint8_t shoulder_c[3], ground_c[3];
} Look;

static const ObstacleStyle k_obstacle = {
    {16, 28, 30}, {22, 40, 42}, {30, 56, 58}, {40, 80, 84}, {20, 200, 190}
};

static const Look k_look[3] = {
    {
        {8, 22, 26}, {14, 56, 60}, {110, 150, 148}, {16, 36, 40}, {2, 6, 8},
        136,
        {0, 0, 0}, {0, 0, 0},
        {26, 30, 34}, {8, 16, 18}, {32, 220, 210}, {70, 140, 146},
        {16, 24, 28}, {6, 10, 12}
    },
    {
        {6, 14, 18}, {10, 40, 48}, {100, 160, 150}, {12, 28, 36}, {1, 4, 6},
        180,
        {0, 0, 0}, {0, 0, 0},
        {20, 26, 30}, {6, 12, 16}, {48, 255, 230}, {80, 160, 156},
        {12, 22, 26}, {4, 8, 10}
    },
    {
        {2, 8, 10}, {6, 22, 26}, {90, 140, 136}, {8, 20, 24}, {0, 2, 4},
        220,
        {0, 0, 0}, {0, 0, 0},
        {14, 18, 22}, {4, 8, 10}, {20, 170, 180}, {50, 100, 110},
        {8, 14, 16}, {2, 4, 6}
    },
};

static Light live;
static RoadStyle road;
static uint8_t sky_top[3];
static int shown = -1;
static int applied_fast = -1;

static void rgb(uint8_t *d, const uint8_t *s) {
    d[0] = s[0];
    d[1] = s[1];
    d[2] = s[2];
}

static void apply(int stage) {
    const Look *L;
    int fast = path_is_straight();
#ifdef RUNNER_LEGACY_WORLDS
    fast = 0;
#endif
#ifdef RUNNER_PREVIEW
    if (world_mesh_preview_legacy) fast = 0;
#endif
    if (stage < 0)
        stage = 0;
    if (stage > 2)
        stage = 2;
    if (stage == shown && fast == applied_fast)
        return;
    shown = stage;
    applied_fast = fast;
    road.analytic_continuation = fast;
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
    if (fast) {
        /* The tunnel has no sky opening. All surfaces converge into darkness. */
        live.sky[0] = live.sky[1] = live.sky[2] = 0;
        live.haze[0] = live.haze[1] = live.haze[2] = 0;
        road.road_far[0] = road.road_far[1] = road.road_far[2] = 0;
    }
}

/* A straight rectangular tunnel is six screen-space faces, not hundreds of
 * sampled world-space sheets. These rays are its exact perspective outline.
 * The floor fills outside the deck when the authored lane width narrows.
 * There is no finite far cap: all four planes end at the vanishing point. */
static void tunnel_face(XY a, XY b, XY c, XY d, const uint8_t *color,
                        int ka, int kb, int kc, int kd, int bucket) {
    int stage_scale = 256 - shown * 32;
    ka = ka * stage_scale >> 8; kb = kb * stage_scale >> 8;
    kc = kc * stage_scale >> 8; kd = kd * stage_scale >> 8;
    poly_g4(&a,&b,&c,&d,bucket,
            color[0]*ka>>8,color[1]*ka>>8,color[2]*ka>>8,
            color[0]*kb>>8,color[1]*kb>>8,color[2]*kb>>8,
            color[0]*kc>>8,color[1]*kc>>8,color[2]*kc>>8,
            color[0]*kd>>8,color[1]*kd>>8,color[2]*kd>>8,BLEND_NONE);
}

static void straight_tube(const LevelFrame *f) {
    enum { HALF = ROAD_HALF + TUBE_OUT, ROOF = 408, RIB_STEP = 2560, END = 20000 };
    static const uint8_t wall[3] = {10,24,28}, ceiling[3] = {6,14,18};
    const XY vp = {SCREEN_W/2,HORIZON_Y};
    const XY tl = {SCREEN_W/2-HORIZON_Y*HALF/(ROOF-CAM_H),0};
    const XY tr = {SCREEN_W-tl.x,0};
    const XY lt = {0,0}, rt = {SCREEN_W,0};
    const XY lb = {0,SCREEN_H}, rb = {SCREEN_W,SCREEN_H};
    const XY lf = {0,HORIZON_Y+(SCREEN_W/2)*CAM_H/HALF};
    const XY rf = {SCREEN_W,lf.y};
    int top_k = (SCREEN_W/2-tl.x)*256/(SCREEN_W/2);
    int floor_k = (lf.y-HORIZON_Y)*256/(SCREEN_H-HORIZON_Y);
    int s = f->scroll / RIB_STEP * RIB_STEP, pulse = (f->time*3)&255;
    int glow = pulse < 128 ? pulse : 255-pulse;
    world_asset("shell_left",0,"quad");
    tunnel_face(lt,tl,lf,vp,wall,256,top_k,256,0,OT_LENGTH-2);
    world_asset("shell_right",0,"quad");
    tunnel_face(tr,rt,vp,rf,wall,top_k,256,0,256,OT_LENGTH-2);
    world_asset("shell_ceiling",0,"triangle");
    tunnel_face(tl,tr,vp,vp,ceiling,top_k,top_k,0,0,OT_LENGTH-2);
    world_asset("shell_floor",0,"three_triangles");
    tunnel_face(vp,lf,lb,lb,road.ground_c,0,floor_k,256,256,OT_LENGTH-2);
    tunnel_face(vp,lb,rb,rb,road.ground_c,0,256,256,256,OT_LENGTH-2);
    tunnel_face(vp,rb,rf,rf,road.ground_c,0,256,floor_k,floor_k,OT_LENGTH-2);

    /* Continuous thin conduits share the same vanishing point and fade there. */
    {
        /* Follow the lower wall, above its floor seam, rather than appearing
         * almost horizontal at eye level. Keep the same perspective endpoint. */
        XY a = {0,lf.y-18}, b = {0,lf.y-16};
        XY c = {SCREEN_W,a.y}, d = {SCREEN_W,b.y};
        uint8_t color[3] = {road.edge[0]/3,road.edge[1]/3,road.edge[2]/3};
        world_asset("conduit",0,"triangle");
        tunnel_face(a,b,vp,vp,color,256,256,0,0,OT_LENGTH-3);
        world_asset("conduit",1,"triangle");
        tunnel_face(c,d,vp,vp,color,256,256,0,0,OT_LENGTH-3);
    }
    if (s < f->scroll + CLIP) s += RIB_STEP;
    for (; s < f->scroll + END && road_scenery_gate_allowed(s); s += RIB_STEP) {
        int z = s-f->scroll, q = (SCREEN_W/2)*4096/z;
        int fade = (END-z)*256/END * 3000/(z+3000);
        int r = (road.edge[0]/2+glow/2)*fade>>8;
        int g = (road.edge[1]/2+glow/2)*fade>>8;
        int b = (road.edge[2]/2+glow/3)*fade>>8;
        int bucket = z < FAR ? z>>OT_SHIFT : (FAR>>OT_SHIFT)+1 +
                     (z-FAR)*(OT_LENGTH-5-(FAR>>OT_SHIFT))/(END-FAR);
        int x = (ROAD_HALF+20)*q>>12;
        int top = HORIZON_Y-((428-CAM_H)*q>>12);
        int foot = HORIZON_Y+(CAM_H*q>>12);
        XY al = {SCREEN_W/2-x,foot}, at = {SCREEN_W/2-x,top};
        XY bt = {SCREEN_W/2+x,top}, bl = {SCREEN_W/2+x,foot};
        world_asset("rib",s,z < 1600 ? "mesh" : "lines");
        if (z < 1600) {
            int q1 = (SCREEN_W/2)*4096/(z+36), x1 = (ROAD_HALF+20)*q1>>12;
            int t1 = HORIZON_Y-((428-CAM_H)*q1>>12), f1 = HORIZON_Y+(CAM_H*q1>>12);
            XY cl = {SCREEN_W/2-x1,f1}, ct = {SCREEN_W/2-x1,t1};
            XY dt = {SCREEN_W/2+x1,t1}, dl = {SCREEN_W/2+x1,f1};
            /* All three surfaces lie outside the viewport, including the
             * trailing section: no part of this near arch can be seen. */
            if (x1 > SCREEN_W/2 && t1 < 0)
                continue;
            poly_f4(&al,&at,&cl,&ct,bucket,r,g,b,BLEND_NONE);
            poly_f4(&at,&bt,&ct,&dt,bucket,r,g,b,BLEND_NONE);
            poly_f4(&bt,&bl,&dt,&dl,bucket,r,g,b,BLEND_NONE);
        } else {
            if (x*2 <= 1 && foot-top <= 1) {
                XY p0 = {vp.x,vp.y}, p1 = {vp.x+1,vp.y};
                XY p2 = {vp.x,vp.y+1}, p3 = {vp.x+1,vp.y+1};
                world_asset("rib",s,"pixel");
                poly_f4(&p0,&p1,&p2,&p3,bucket,r,g,b,BLEND_NONE);
                continue;
            }
            line_f2(al,at,bucket,r,g,b);
            line_f2(at,bt,bucket,r,g,b);
            line_f2(bt,bl,bucket,r,g,b);
        }
    }
}

static void sine_clear(const LevelFrame *f, int *r, int *g, int *b) {
    apply(f->stage);
    *r = live.sky[0];
    *g = live.sky[1];
    *b = live.sky[2];
}

/* Walls and ceiling share their edges, so the tube is closed and the
 * clear color shows only at the far mouth. The sheets run as far as the
 * road's far deck. One rib and a conduit stripe stay beside the runner;
 * both pulse with the clock. */
static void tube(const LevelFrame *f) {
    int step = 640;
    int reach = ROAD_HALF + TUBE_OUT;
    int s = f->scroll / step * step;
    int s1, draw;
    int pulse = (f->time * 3) & 255;
    int glow = pulse < 128 ? pulse : 255 - pulse;
    int wr = road.edge[0] / 2 + glow / 2;
    int wg = road.edge[1] / 2 + glow / 2;
    int wb = road.edge[2] / 2 + glow / 3;
    int conduit = 1;
    int rib_s = f->scroll / (step * 4) * (step * 4);
    if (rib_s < f->scroll + 280)
        rib_s += step * 4;
    for (; (draw = road_span(s, f->scroll, step, reach, 440, &s1)) != 0; s = s1) {
        if (draw == 2)
            continue;
        if (!road_cloth(s, s1, -1, TUBE_OUT, 0, TUBE_OUT, 420, 6, 12, 14))
            return;
        if (!road_cloth(s, s1, 1, TUBE_OUT, 0, TUBE_OUT, 420, 6, 12, 14))
            return;
        if (!road_lintel(s, s1, TUBE_OUT, 408, 5, 10, 12))
            return;
        if (!conduit || s > f->scroll + 1600) {
            continue;
        }
        conduit = 0;
        if (!road_cloth(s, s1, -1, TUBE_OUT - 2, 150, TUBE_OUT - 2, 176, wr, wg, wb))
            return;
        if (!road_cloth(s, s1, 1, TUBE_OUT - 2, 150, TUBE_OUT - 2, 176, wr, wg, wb))
            return;
    }
    if (rib_s < f->scroll + DETAIL_Z && road_scenery_gate_allowed(rib_s)) {
        world_asset("rib",rib_s,"mesh");
        if (!road_cloth(rib_s, rib_s + 36, -1, 20, 0, 20, 440, wr, wg, wb))
            return;
        if (!road_cloth(rib_s, rib_s + 36, 1, 20, 0, 20, 440, wr, wg, wb))
            return;
        road_lintel(rib_s, rib_s + 36, 20, 428, wr, wg, wb);
    }
}

static void sine_draw(const LevelFrame *f) {
    int hy;
    apply(f->stage);
    if (applied_fast) {
        world_asset("road",0,"mesh");
        road_draw(&road,f);
        world_asset("road_vanish",0,"triangle");
        road_to_vanishing_point(f->scroll);
        PROF(PF_WORLD_MESH, straight_tube(f));
        world_asset("speed_lines",0,"lines");
        road_streaks(f,road.edge[0],road.edge[1],road.edge[2],-1,0);
        return;
    }
    hy = path_horizon();
    if (hy < 8)
        hy = 8;
    if (hy > SCREEN_H - 8)
        hy = SCREEN_H - 8;
    gradient(0, 0, SCREEN_W, hy, OT_LENGTH - 1,
             sky_top[0], sky_top[1], sky_top[2],
             live.sky[0], live.sky[1], live.sky[2]);
    road_draw(&road, f);
    tube(f);
    road_streaks(f, road.edge[0], road.edge[1], road.edge[2], -1, 0);
}

static const Stage k_stages[] = {
    {"THE GATE", "sine.gate", 0, STAGE_FINISH},
    {"THE BEAT", "sine.beat", 0, STAGE_FINISH},
    {"THE CRUSH", "sine.crush", 0, STAGE_FINISH},
};

const Level level_sine = {
    "THE SINEWORKS",
    "sine.gate",
    sine_clear,
    sine_draw,
    &live,
    1,
    k_stages,
    3,
};

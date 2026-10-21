/* A level is the scenery around the shared course. To add one, write a
 * level_<name>.c that defines a Level, and list it in levels.c. */

#pragma once

#include <stdint.h>

typedef struct {
    int scroll; /* distance run; camera z plus scroll is world z */
    int speed;  /* scroll gained this frame, 0 while stopped */
    int time;   /* animation clock; holds still while the game is frozen */
    int x;      /* runner's lateral offset from the centerline */
    int stage;  /* story stage under this world; 0 when the world has one look */
} LevelFrame;

/* Story roads share this finish. A `beyond` piece runs on past it. */
#define STAGE_FINISH 108544

typedef struct {
    const char *name;
    const char *track;
    int start;
    int finish;
} Stage;

/* The light a world casts, in one place. Everything the world draws and the
 * runner it carries take their color from here.
 *
 * sky and haze are plain colors. tint and shadow are GPU multipliers, where
 * 128 leaves a channel unchanged: tint lights the runner sprite and the road
 * paint, and shadow is how much the runner's shadow removes from each
 * channel of the ground under it. density is how thick the air is, 0..256:
 * at 0 distance fades each material toward its own far color; as it rises,
 * those far colors, and the backdrop, give way to the haze. */
typedef struct {
    uint8_t sky[3];    /* the empty world: clear color behind everything */
    uint8_t haze[3];   /* the air: distance fades toward it */
    uint8_t tint[3];   /* the light on the runner and the paint */
    uint8_t shadow[3]; /* the color of what the runner blocks */
    int density;       /* how much of the far world the air swallows */
} Light;

/* Untouched light, for a world that names none. */
extern const Light light_neutral;

typedef struct {
    const char *name;
    /* Track path_open runs for this world. */
    const char *track;
    /* Fills the screen before anything else draws. */
    void (*clear_color)(const LevelFrame *f, int *r, int *g, int *b);
    /* Draws the ground, the scenery, and the obstacles from course.h. */
    void (*draw)(const LevelFrame *f);
    /* The world's light, or 0 for light_neutral. */
    const Light *light;
    /* 1 if Story Mode offers this world. Arcade Mode offers the others. */
    int story;
    /* Story stages on this world. Arcade worlds leave these empty. */
    const Stage *stages;
    int stage_count;
} Level;

extern const Level *const levels[];
extern const int level_count;

/* The light of a level, never 0. */
const Light *level_light(const Level *lv);

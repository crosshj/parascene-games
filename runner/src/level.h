/* A level is the scenery around the shared course. To add one, write a
 * level_<name>.c that defines a Level, and list it in levels.c. */

#pragma once

typedef struct {
    int scroll; /* distance run; camera z plus scroll is world z */
    int speed;  /* scroll gained this frame, 0 while stopped */
    int time;   /* animation clock; holds still while the game is frozen */
} LevelFrame;

typedef struct {
    const char *name;
    /* Track path_open runs for this world. */
    const char *track;
    /* Fills the screen before anything else draws. */
    void (*clear_color)(const LevelFrame *f, int *r, int *g, int *b);
    /* Draws the ground, the scenery, and the obstacles from course.h. */
    void (*draw)(const LevelFrame *f);
} Level;

extern const Level *const levels[];
extern const int level_count;

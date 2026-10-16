#include <string.h>
#include "path.h"
#include "course.h"
#include "track_data.h"

static int path_scroll;

typedef char path_step_ok[(STEP == 256) ? 1 : -1];
typedef char path_span_ok[(TRACK_SPAN == TRACK_LEAD + TRACK_CYCLE) ? 1 : -1];

int path_open(const char *id) {
    return id && strcmp(id, TRACK_ID) == 0;
}

void path_begin(int scroll) {
    path_scroll = scroll;
}

void path_cam(int s, int u, int v, int *x, int *y, int *z) {
    *x = u;
    *y = v;
    *z = s - path_scroll;
}

int path_project(int s, int u, int v, XY *out) {
    int x, y, z;
    path_cam(s, u, v, &x, &y, &z);
    return project(x, y, z, out);
}

const PathSeg *path_at(int s) {
    int band;
    if (s < 0)
        return 0;
    band = s / STEP;
    if (band < TRACK_LEAD)
        return &track_span[band];
    return &track_span[TRACK_LEAD + (band - TRACK_LEAD) % TRACK_CYCLE];
}

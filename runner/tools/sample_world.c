/* Host-only adapter: use the game's exact fixed-point path arithmetic.
 * Including path.c deliberately gives the compiler access to its world-space
 * deck and camera frames without adding an authoring API to the console. */
#include <stdio.h>
#include <stdlib.h>
#include "../src/path.c"

int project(int x, int y, int z, XY *out) { return z; }

int main(int argc, char **argv) {
    int s, i, limit, outside, wall, ceiling, step;
    if (argc != 7 || !path_open(argv[1]) || path_branches() || path_lap()) {
        fprintf(stderr, "sample_world: expected a finite, unbranched track\n");
        return 1;
    }
    step = atoi(argv[2]); limit = atoi(argv[3]);
    outside = atoi(argv[4]); wall = atoi(argv[5]); ceiling = atoi(argv[6]);
    if (step != STEP || limit < 0 || limit > 1000000)
        return 1;
    for (s = 1; s < path_len(); s++)
        if (path_cut(s)) {
            fprintf(stderr, "sample_world: discontinuous paths need separate mesh sections\n");
            return 1;
        }
    printf("{\"length\":%d,\"rings\":[", path_len() * STEP);
    for (s = 0; s <= limit; s += step) {
        int ul, ur;
        path_edges(s, &ul, &ur);
        printf("%s[", s ? "," : "");
        for (i = 0; i < 6; i++) {
            int dx, dy, dz, rx, ry, rz;
            int u = i < 3 ? ul - outside : ur + outside;
            int v = i == 0 || i == 5 ? 0 : i == 1 || i == 4 ? wall : ceiling;
            deck_at(s, v, &dx, &dy, &dz, &rx, &ry, &rz);
            printf("%s[%d,%d,%d]", i ? "," : "", dx + cam_x + mul_q(rx, u),
                   dy + cam_y + mul_q(ry, u), dz + cam_z + mul_q(rz, u));
        }
        printf("]");
    }
    printf("],\"cameras\":[");
    /* Three poses per schedule interval, including its far endpoint. */
    for (s = 0; s <= limit; s += step / 2) {
        path_begin(s);
        printf("%s[%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d]", s ? "," : "",
               cam_x, cam_y, cam_z, cam_rx, cam_ry, cam_rz,
               cam_ux, cam_uy, cam_uz, cam_fx, cam_fy, cam_fz);
    }
    printf("]}\n");
    return 0;
}

/* Host integration check: independently sample the runtime path and compare
 * baked vertices after the real camera transform, including seeks/restarts. */
#include <stdio.h>
#include <string.h>
#include "path.h"
#include "course.h"
#include "world_mesh.h"
#include "world_mesh_data.h"

int project(int x, int y, int z, XY *out) { return z; }

int main(void) {
    unsigned m;
    for (m = 0; m < WORLD_MESH_COUNT; m++) {
        const WorldMesh *mesh = &world_meshes[m];
        int band, pose, i, j;
        if (!path_open(mesh->track))
            return 1;
        /* Reverse band order and jump between offsets; no forward cursor. */
        for (band = mesh->schedule_count - 1; band >= 0; band--) {
            const WorldSchedule *schedule = &mesh->schedules[band];
            int base = band > 0 ? band - 1 : 0;
            if (mesh->version != 1 || schedule->count < 2 ||
                mesh->indices[schedule->first] != 0)
                return 1;
            for (pose = 0; pose < 3; pose++) {
                path_begin(band * STEP + (pose == 0 ? 255 : pose == 1 ? 0 : 127));
                for (i = 0; i < schedule->count; i++) {
                    int relative = mesh->indices[schedule->first + i];
                    int index = base + relative;
                    const WorldRing *ring = &mesh->rings[index];
                    int ul, ur;
                    if (i && (relative <= mesh->indices[schedule->first + i - 1] ||
                              relative - mesh->indices[schedule->first + i - 1] > 8))
                        return 1;
                    path_edges(index * STEP, &ul, &ur);
                    for (j = 0; j < 6; j++) {
                        int x, y, z, bx, by, bz;
                        int height = j == 0 || j == 5 ? 0 : j == 1 || j == 4 ? 420 : 408;
                        path_cam(index * STEP, j < 3 ? ul - 36 : ur + 36, height, &x, &y, &z);
                        path_transform(ring->origin.x + (j ? ring->offset[j - 1][0] : 0),
                                       ring->origin.y + (j ? ring->offset[j - 1][1] : 0),
                                       ring->origin.z + (j ? ring->offset[j - 1][2] : 0), &bx, &by, &bz);
                        if (iabs(x - bx) > 1 || iabs(y - by) > 1 || iabs(z - bz) > 1) {
                            fprintf(stderr, "%s band %d ring %d corner %d: baked geometry mismatch\n",
                                    mesh->track, band, index, j);
                            return 1;
                        }
                    }
                }
            }
            if ((base + mesh->indices[schedule->first + schedule->count - 1]) * STEP < band * STEP + 44000)
                return 1;
        }
    }
    puts("world mesh: camera transforms, reverse seeks, LOD order and distant coverage ok");
    return 0;
}

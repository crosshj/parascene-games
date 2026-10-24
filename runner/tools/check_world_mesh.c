/* Host integration check: independently sample the runtime path and compare
 * baked vertices after the real camera transform, including seeks/restarts. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "path.h"
#include "course.h"
#include "world_mesh.h"
#ifndef WORLD_MESH_HEADER
#define WORLD_MESH_HEADER "world_mesh_data.h"
#endif
#include WORLD_MESH_HEADER
#ifdef WORLD_ROAD_MESH
#define world_meshes road_world_meshes
#define WORLD_MESH_COUNT ROAD_WORLD_MESH_COUNT
#endif

int project(int x, int y, int z, XY *out) { return z; }

int main(int argc,char **argv) {
    int profile[18]={-1,36,0,-1,36,420,-1,36,408,1,36,408,1,36,420,1,36,0};
    int lookahead=44000,max_stride=8;
    if(argc==4) {
        char *c=argv[3];int i;lookahead=atoi(argv[1]);max_stride=atoi(argv[2]);
        for(i=0;i<18;i++) { profile[i]=(int)strtol(c,&c,10);if(i<17 && *c++!=',') return 1; }
    }
    unsigned m;
    unsigned long lod_checks=0;double worst_error=0;
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
                              relative - mesh->indices[schedule->first + i - 1] > max_stride))
                        return 1;
                    path_edges(index * STEP, &ul, &ur);
                    for (j = 0; j < 6; j++) {
                        int x, y, z, bx, by, bz;
                        int height=profile[j*3+2],outside=profile[j*3+1];
                        path_cam(index * STEP, profile[j*3]<0 ? ul-outside : ur+outside, height, &x, &y, &z);
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
            /* Independent camera and geometry positions, between the compiler's
             * samples. Check the projected silhouette of the actual mesh chord. */
            if(argc==4) for(pose=0;pose<2;pose++) {
                int camera=band*STEP+(pose?191:63);
                path_begin(camera);
                for(i=1;i<schedule->count;i++) {
                    int ia=base+mesh->indices[schedule->first+i-1];
                    int ib=base+mesh->indices[schedule->first+i];
                    int at,span=(ib-ia)*STEP;
                    if(ia*STEP<camera+mesh->near_distance) continue;
                    for(at=37;at<span;at+=113) for(j=0;j<6;j++) {
                        const WorldRing *a=&mesh->rings[ia],*b=&mesh->rings[ib];
                        int ul,ur,x,y,z,ax,ay,az,bx,by,bz;
                        double qx,qy,qz,error,px,py;
                        path_edges(ia*STEP+at,&ul,&ur);
                        path_cam(ia*STEP+at,profile[j*3]<0?ul-profile[j*3+1]:ur+profile[j*3+1],profile[j*3+2],&x,&y,&z);
                        path_transform(a->origin.x+(j?a->offset[j-1][0]:0),a->origin.y+(j?a->offset[j-1][1]:0),a->origin.z+(j?a->offset[j-1][2]:0),&ax,&ay,&az);
                        path_transform(b->origin.x+(j?b->offset[j-1][0]:0),b->origin.y+(j?b->offset[j-1][1]:0),b->origin.z+(j?b->offset[j-1][2]:0),&bx,&by,&bz);
                        qx=ax+(bx-ax)*(double)at/span;qy=ay+(by-ay)*(double)at/span;qz=az+(bz-az)*(double)at/span;
                        if(z<64||qz<64) continue;
                        px=160+x*160.0/z;py=72+(170-y)*160.0/z;
                        if(px<0||px>=320||py<0||py>=240) continue;
                        error=fmax(fabs(px-(160+qx*160/qz)),fabs(py-(72+(170-qy)*160/qz)));
                        lod_checks++;if(error>worst_error) worst_error=error;
                        if(error>1.1) { fprintf(stderr,"mesh chord error %.5f px at %d span %d+%d\n",error,camera,ia*STEP,span);return 1; }
                    }
                }
            }
            if ((base + mesh->indices[schedule->first + schedule->count - 1]) * STEP < band * STEP + lookahead)
                return 1;
        }
    }
    if(lod_checks) printf("mesh LOD: %lu independent visible samples, worst %.6f pixels\n",lod_checks,worst_error);
    puts("world mesh: camera transforms, reverse seeks, LOD order and distant coverage ok");
    return 0;
}

/* Host integration check: independently sample the runtime path and compare
 * baked vertices after the real camera transform, including seeks/restarts. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "path.h"
#include "course.h"
#include "world_mesh.h"
#ifndef WORLD_LOD_ERROR_LIMIT
#define WORLD_LOD_ERROR_LIMIT 1.1
#endif
#ifndef WORLD_MESH_HEADER
#define WORLD_MESH_HEADER "world_mesh_data.h"
#endif
#include WORLD_MESH_HEADER
#ifdef WORLD_ROAD_MESH
#define world_meshes road_world_meshes
#define WORLD_MESH_COUNT ROAD_WORLD_MESH_COUNT
#endif

int project(int x, int y, int z, XY *out) { return z; }

static int height_at(int s,int configured) {
#ifdef WORLD_HEIGHT_CRACK
    if(configured>0) {
        const PathSeg *lip=path_at(0),*seg=path_at(s);
        int drop=lip&&seg?lip->y-seg->y:0;
        if(drop<0) drop=0;
        drop=220+drop/2;
        return drop>960?960:drop;
    }
#endif
    return configured;
}

static unsigned mesh_stride(const WorldMesh *mesh,uint32_t bit) {
    unsigned byte=bit>>3,shift=bit&7;
    unsigned value=mesh->indices[byte]|((unsigned)mesh->indices[byte+1]<<8);
    return ((value>>shift)&((1u<<mesh->stride_bits)-1))+1;
}

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
        path_reset();
        if(mesh->fork_wall_count && !mesh->fork_walls) {
            fprintf(stderr,"%s: fork wall mesh has no geometry\n",mesh->track);
            return 1;
        }
        for(i=0;i<mesh->fork_wall_count;i++) {
            const WorldForkWall *w=&mesh->fork_walls[i];
            WorldVertex points[8];int corner;
            for(corner=0;corner<8;corner++) points[corner]=world_fork_vertex(w,corner);
            if(w->start<0 || w->end<=w->start || w->end>path_len()*STEP) {
                fprintf(stderr,"%s: invalid baked fork wall segment %d\n",mesh->track,i);
                return 1;
            }
            for(j=0;j<2;j++) {
                const WorldVertex *a=&points[j*2],*b=&points[j*2+1];
                const WorldVertex *ta=&points[j*2+4],*tb=&points[j*2+5];
                const WorldVertex *oa=&points[(1-j)*2],*ob=&points[(1-j)*2+1];
                const WorldVertex *ota=&points[(1-j)*2+4],*otb=&points[(1-j)*2+5];
                long long ux=b->x-a->x,uy=b->y-a->y,uz=b->z-a->z;
                long long vx=ta->x-a->x,vy=ta->y-a->y,vz=ta->z-a->z;
                long long cx=uy*vz-uz*vy,cy=uz*vx-ux*vz,cz=ux*vy-uy*vx;
                if(a->x==oa->x&&a->y==oa->y&&a->z==oa->z&&
                   b->x==ob->x&&b->y==ob->y&&b->z==ob->z&&
                   ta->x==ota->x&&ta->y==ota->y&&ta->z==ota->z&&
                   tb->x==otb->x&&tb->y==otb->y&&tb->z==otb->z) continue;
                if(cx*cx+cy*cy+cz*cz<=256 || ta->y<=a->y || tb->y<=b->y) {
                    fprintf(stderr,"%s: invalid fork wall surface %d/%d\n",mesh->track,i,j);
                    return 1;
                }
            }
            if(i>0 && mesh->fork_walls[i-1].flags==w->flags) {
                const WorldForkWall *prev=&mesh->fork_walls[i-1];
                int edge;
                for(edge=0;edge<4;edge++) {
                    WorldVertex previous_point=world_fork_vertex(prev,edge*2+1);
                    const WorldVertex *a=&previous_point,*b=&points[edge*2];
                    long long dx=a->x-b->x,dy=a->y-b->y,dz=a->z-b->z;
                    if(prev->end!=w->start || dx || dy || dz) {
                        fprintf(stderr,"%s: discontinuous fork edge at %d\n",mesh->track,w->start);
                        return 1;
                    }
                }
            }
        }
        for(j=0;j<path_branches() && j<3;j++)
            if(mesh->route_mask&(1u<<j)) path_swap(j);
        /* Reverse band order and jump between offsets; no forward cursor. */
        for (band = mesh->schedule_count - 1; band >= 0; band--) {
            const WorldSchedule *schedule = &mesh->schedules[band];
            int base = band > 0 ? band - 1 : 0;
            int schedule_ring[128],relative=schedule->first_offset;
            uint32_t bit=schedule->first;
            if (mesh->version != 1 || schedule->count < 2 ||
                schedule->count>128) {
                fprintf(stderr,"%s band %d: invalid schedule version/count %u/%u\n",
                        mesh->track,band,mesh->version,schedule->count);
                return 1;
            }
            for(i=0;i<schedule->count;i++) {
                if(i) { relative+=(int)mesh_stride(mesh,bit);bit+=mesh->stride_bits; }
                schedule_ring[i]=base+relative;
                if(i && relative-(schedule_ring[i-1]-base)>max_stride) {
                    fprintf(stderr,"%s band %d: packed stride %d exceeds %d\n",mesh->track,band,
                            relative-(schedule_ring[i-1]-base),max_stride);
                    return 1;
                }
            }
            for (pose = 0; pose < 3; pose++) {
                path_begin(band * STEP + (pose == 0 ? 255 : pose == 1 ? 0 : 127));
                for (i = 0; i < schedule->count; i++) {
                    int index = schedule_ring[i];
                    const WorldRing *ring = world_mesh_ring(mesh,index);
                    int ul, ur;
                    path_edges(index * STEP, &ul, &ur);
                    for (j = 0; j < 6; j++) {
                        int x, y, z, bx, by, bz;
                        int height=height_at(index*STEP,profile[j*3+2]),outside=profile[j*3+1];
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
                    int ia=schedule_ring[i-1];
                    int ib=schedule_ring[i];
                    int at,span=(ib-ia)*STEP;
                    if(world_mesh_ring(mesh,ib)->flags&WORLD_RING_CUT_BEFORE) continue;
                    if(ia*STEP<camera+mesh->near_distance) continue;
                    for(at=37;at<span;at+=113) for(j=0;j<6;j++) {
                        const WorldRing *a=world_mesh_ring(mesh,ia),*b=world_mesh_ring(mesh,ib);
                        int ul,ur,x,y,z,ax,ay,az,bx,by,bz;
                        double qx,qy,qz,error,px,py;
                        path_edges(ia*STEP+at,&ul,&ur);
                        path_cam(ia*STEP+at,profile[j*3]<0?ul-profile[j*3+1]:ur+profile[j*3+1],height_at(ia*STEP+at,profile[j*3+2]),&x,&y,&z);
                        path_transform(a->origin.x+(j?a->offset[j-1][0]:0),a->origin.y+(j?a->offset[j-1][1]:0),a->origin.z+(j?a->offset[j-1][2]:0),&ax,&ay,&az);
                        path_transform(b->origin.x+(j?b->offset[j-1][0]:0),b->origin.y+(j?b->offset[j-1][1]:0),b->origin.z+(j?b->offset[j-1][2]:0),&bx,&by,&bz);
                        qx=ax+(bx-ax)*(double)at/span;qy=ay+(by-ay)*(double)at/span;qz=az+(bz-az)*(double)at/span;
                        if(z<64||qz<64) continue;
                        px=160+x*160.0/z;py=72+(170-y)*160.0/z;
                        if(px<0||px>=320||py<0||py>=240) continue;
                        error=fmax(fabs(px-(160+qx*160/qz)),fabs(py-(72+(170-qy)*160/qz)));
                        lod_checks++;if(error>worst_error) worst_error=error;
                        if(error>WORLD_LOD_ERROR_LIMIT) { fprintf(stderr,"mesh chord error %.5f px at %d span %d+%d\n",error,camera,ia*STEP,span);return 1; }
                    }
                }
            }
            if (schedule_ring[schedule->count-1] * STEP < band * STEP + lookahead) {
                fprintf(stderr,"%s band %d: schedule ends at %d before lookahead %d\n",
                        mesh->track,band,schedule_ring[schedule->count-1]*STEP,band*STEP+lookahead);
                return 1;
            }
        }
    }
    if(lod_checks) printf("mesh LOD: %lu independent visible samples, worst %.6f pixels\n",lod_checks,worst_error);
    puts("world mesh: camera transforms, reverse seeks, LOD order and distant coverage ok");
    return 0;
}

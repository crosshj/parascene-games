/* Host-only adapter: use the game's exact fixed-point path arithmetic.
 * Including path.c deliberately gives the compiler access to its world-space
 * deck and camera frames without adding an authoring API to the console. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RUNNER_BAKE_PATH 1
#include "../src/path.c"

int project(int x, int y, int z, XY *out) { return z; }

static void ring_at(int s,int outside,int wall,int ceiling,const int *profile,
                    int custom,int crack_height,int points[6][3]) {
    int i,ul,ur;
    path_edges(s,&ul,&ur);
    for(i=0;i<6;i++) {
        int dx,dy,dz,rx,ry,rz;
        int u=i<3?ul-outside:ur+outside;
        int v=i==0||i==5?0:i==1||i==4?wall:ceiling;
        if(custom) {
            int side=profile[i*3];
            u=side<0?ul-profile[i*3+1]:ur+profile[i*3+1];
            v=profile[i*3+2];
            if(crack_height&&v>0) {
                const PathSeg *lip=path_at(0),*seg=path_at(s);
                int drop=lip&&seg?lip->y-seg->y:0;
                if(drop<0) drop=0;
                v=220+drop/2;
                if(v>960) v=960;
            }
        }
        deck_at(s,v,&dx,&dy,&dz,&rx,&ry,&rz);
        points[i][0]=dx+cam_x+mul_q(rx,u);
        points[i][1]=dy+cam_y+mul_q(ry,u);
        points[i][2]=dz+cam_z+mul_q(rz,u);
    }
}

int main(int argc, char **argv) {
    int s, i, limit, outside, wall, ceiling, step, crack_height, route;
    int profile[18];
    if ((argc != 7 && argc != 8 && argc != 9) || !path_open(argv[1]) || path_lap()) {
        fprintf(stderr, "sample_world: expected a finite track\n");
        return 1;
    }
    route = getenv("RUNNER_BAKE_ROUTE") ? atoi(getenv("RUNNER_BAKE_ROUTE")) : 0;
    if (route < 0 || route >= (1<<path_branches())) return 1;
    for(i=0;i<path_branches() && i<3;i++) if(route&(1<<i)) path_swap(i);
    if (argc >= 8 && sscanf(argv[7], "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
        &profile[0],&profile[1],&profile[2],&profile[3],&profile[4],&profile[5],
        &profile[6],&profile[7],&profile[8],&profile[9],&profile[10],&profile[11],
        &profile[12],&profile[13],&profile[14],&profile[15],&profile[16],&profile[17]) != 18) return 1;
    crack_height = argc == 9 && !strcmp(argv[8], "crack");
    if (argc == 9 && !crack_height) return 1;
    step = atoi(argv[2]); limit = atoi(argv[3]);
    outside = atoi(argv[4]); wall = atoi(argv[5]); ceiling = atoi(argv[6]);
    if (step != STEP || limit < 0 || limit > 1000000)
        return 1;
    printf("{\"length\":%d,\"rings\":[", path_len() * STEP);
    for (s = 0; s <= limit; s += step) {
        int points[6][3];
        ring_at(s,outside,wall,ceiling,profile,argc>=8,crack_height,points);
        printf("%s[", s ? "," : "");
        for(i=0;i<6;i++) printf("%s[%d,%d,%d]",i?",":"",points[i][0],points[i][1],points[i][2]);
        printf("]");
    }
    if(crack_height) {
        printf("],\"fine_rings\":[");
        for(s=0;s<=limit;s+=32) {
            int points[6][3];
            ring_at(s,outside,wall,ceiling,profile,1,1,points);
            printf("%s[",s?",":"");
            for(i=0;i<6;i++) printf("%s[%d,%d,%d]",i?",":"",points[i][0],points[i][1],points[i][2]);
            printf("]");
        }
    }
    printf("],\"cuts\":[");
    for(s=0;s<=limit;s+=step)
        printf("%s%d",s?",":"",s>0&&path_cut(s/step));
    printf("],\"cameras\":[");
    /* Five poses per schedule interval, including both endpoints. */
    for (s = 0; s <= limit; s += step / 4) {
        path_begin(s);
        printf("%s[%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d]", s ? "," : "",
               cam_x, cam_y, cam_z, cam_rx, cam_ry, cam_rz,
               cam_ux, cam_uy, cam_uz, cam_fx, cam_fy, cam_fz);
    }
    printf("],\"camera_bounds\":[");
    /* The camera's input is integer progress. Enumerating every position in
     * a band gives exact component bounds for the supported fixed camera. */
    for(s=0;s<=limit;s+=STEP) {
        int lo[12],hi[12],at,j;
        for(at=0;at<=STEP;at++) {
            int v[12];
            path_begin(s+at);
            v[0]=cam_x;v[1]=cam_y;v[2]=cam_z;
            v[3]=cam_rx;v[4]=cam_ry;v[5]=cam_rz;
            v[6]=cam_ux;v[7]=cam_uy;v[8]=cam_uz;
            v[9]=cam_fx;v[10]=cam_fy;v[11]=cam_fz;
            for(j=0;j<12;j++) {
                if(!at||v[j]<lo[j]) lo[j]=v[j];
                if(!at||v[j]>hi[j]) hi[j]=v[j];
            }
        }
        printf("%s[",s?",":"");
        for(j=0;j<24;j++) printf("%s%d",j?",":"",j<12?lo[j]:hi[j-12]);
        putchar(']');
    }
    printf("]}\n");
    return 0;
}

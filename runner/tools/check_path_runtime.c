/* Emit exact path observations. Compiled and reference preparation must
 * agree for route combinations, cuts, loop seams, reverse seeks and resets. */
#include <stdio.h>
#include <stdint.h>
#include "../src/path.c"
int project(int x,int y,int z,XY *p) { return z; }
static uint64_t hash=1469598103934665603ULL;
static void add(int n) { hash^=(uint32_t)n; hash*=1099511628211ULL; }
int main(void) {
    int i,mask,b,offset,j,side;
    static const int offsets[]={0,1,127,255};
    for(i=0;i<TRACK_TABLE_COUNT;i++) {
        path_open(track_table[i].id);
        for(mask=0;mask<(1<<path_branches());mask++) {
            path_reset();
            for(j=0;j<path_branches();j++) if(mask&(1<<j)) path_swap(j);
            for(b=path_len()+65;b>=0;b--) for(offset=0;offset<4;offset++) {
                int s=b*STEP+offsets[offset],x,y,z,l,r;
                path_begin(s);
                add(path_cut(b)); add(path_may_crate(b)); add(path_first_cut(b,b+64));
                for(side=0;side<2;side++) {
                    int ax,ay,az,bx,by,bz;
                    path_end_side(side);
                    path_edges(s+STEP,&l,&r); add(l); add(r);
                    path_cam_pair(s+STEP,l-980,-760,r+18,4096,&ax,&ay,&az,&bx,&by,&bz);
                    path_cam(s+STEP,l-980,-760,&x,&y,&z); add(x);add(y);add(z);
                    if(x!=ax||y!=ay||z!=az) return 1;
                    path_cam(s+STEP,r+18,4096,&x,&y,&z); add(x);add(y);add(z);
                    if(x!=bx||y!=by||z!=bz) return 1;
                }
                path_end_side(0);
            }
        }
        printf("%s %016llx\n",track_table[i].id,(unsigned long long)hash);
    }
    return 0;
}

/* Execute the reference preparation and geometry on the build machine.
 * This is also the independent legacy side of the runtime equivalence test. */
#include <stdio.h>
#include <stdlib.h>
#define RUNNER_BAKE_PATH 1
#include "../src/path.c"
#include "../src/path_runtime.h"

int project(int x, int y, int z, XY *out) { return z; }

static void numbers(const void *data, int n, int width, int sign) {
    int i;
    putchar('[');
    for (i=0;i<n;i++) {
        int v = width==2 ? ((const int16_t *)data)[i] :
            sign ? ((const int8_t *)data)[i] : ((const uint8_t *)data)[i];
        printf("%s%d",i?",":"",v);
    }
    putchar(']');
}

int main(void) {
    int i,j,k,n,extent;
    printf("{\"version\":%d,\"tracks\":[",PATH_RUNTIME_VERSION);
    for(i=0;i<TRACK_TABLE_COUNT;i++) {
        path_open(track_table[i].id);
        n=active->count;
        for(k=0;k<active->branches;k++) {
            int end=active->branch[k].alt+active->branch[k].band1-active->branch[k].band0;
            if(end>n) n=end;
        }
        if(!framed || active->count>BAND_MAX || active->branches>MAX_BRANCHES) {
            fprintf(stderr,"%s exceeds compiled path capacity\n",active->id); return 1;
        }
        extent=0;
        for(j=0;j<n;j++) {
            const PathSeg *p=&active->seg[j];
            int edge[4]={p->el,p->er,p->el1,p->er1};
            for(k=0;k<4;k++) if(iabs(edge[k])>extent) extent=iabs(edge[k]);
        }
        printf("%s{\"id\":\"%s\",\"bands\":%d,\"segments\":%d,\"edge_extent\":%d,\"frames\":[",i?",":"",active->id,active->count,n,extent);
        for(j=0;j<n;j++) { if(j) putchar(','); numbers(seg_frame[j],6,2,1); }
        printf("],\"branches\":"); numbers(band_branch,active->count,1,1);
        printf(",\"flags\":"); numbers(band_flags,active->count,1,0);
        printf(",\"clear\":"); numbers(band_clear,active->count,1,0);
        printf(",\"taper\":"); numbers(seg_taper,n,1,0);
        putchar('}');
    }
    printf("]}\n");
    return 0;
}

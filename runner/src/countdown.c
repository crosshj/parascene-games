/* Monoline sans-serif numerals drawn as vectors, never enlarged font sprites. */
#include "world.h"
#include "countdown.h"

static const XY three[]={{-22,-30},{-12,-38},{6,-38},{20,-30},{22,-18},{16,-7},{3,0},
                         {16,5},{24,17},{22,29},{11,38},{-9,38},{-23,30}};
static const XY two[]={{-23,-25},{-20,-33},{-9,-39},{7,-39},{20,-31},{23,-19},
                       {18,-8},{6,4},{-9,17},{-22,32},{-23,38},{23,38}};
static const XY one[]={{-13,-24},{4,-38},{4,38}};
static int root(int n) {
    int r=0;
    while((r+1)*(r+1)<=n) r++;
    return r ? r : 1;
}
static void stroke(const XY *points,int n,int radius,int color,int scale,int coverage,int seed) {
    int i,j,primitive=0;
    int diag=radius*181/256;
    XY cap[8]={{radius,0},{diag,diag},{0,radius},{-diag,diag},
               {-radius,0},{-diag,-diag},{0,-radius},{diag,-diag}};
    for(i=0;i<n;i++) {
        XY c={160+points[i].x*9*scale/1600,116+points[i].y*9*scale/1600};
        for(j=0;j<8;j++) {
            XY a={c.x+cap[j].x,c.y+cap[j].y};
            XY b={c.x+cap[(j+1)&7].x,c.y+cap[(j+1)&7].y};
            unsigned h=(unsigned)(primitive++ + seed*7919)*0x9e3779b9u;
            h^=h>>16;
            if((h&255u)<(unsigned)coverage)
                poly_f4(&c,&a,&b,&b,1,color,color,color,BLEND_NONE);
        }
        if(i+1<n) {
            int dx=(points[i+1].x-points[i].x)*9*scale/1600;
            int dy=(points[i+1].y-points[i].y)*9*scale/1600;
            int length=root(dx*dx+dy*dy),nx=-dy*radius/length,ny=dx*radius/length;
            XY p[4]={{c.x+nx,c.y+ny},{c.x-nx,c.y-ny},
                     {c.x+dx+nx,c.y+dy+ny},{c.x+dx-nx,c.y+dy-ny}};
            unsigned h=(unsigned)(primitive++ + seed*7919)*0x9e3779b9u;
            h^=h>>16;
            if((h&255u)<(unsigned)coverage)
                poly_f4v(p,1,color,color,color,BLEND_NONE);
        }
    }
}
void countdown_draw(int digit,int age) {
    const XY *p;int n,scale,fade,outer,inner;
    if(age<0) age=0;
    if(age>59) age=59;
    if(age<12) {
        int left=12-age;
        scale=72+28*(144-left*left)/144;
    } else scale=100;
    fade=age<36 ? 256 : (59-age)*256/23;
    outer=5*scale/100; inner=3*scale/100;
    if(outer<1) outer=1;
    if(inner<1) inner=1;
    if(digit==3) {p=three;n=sizeof three/sizeof three[0];}
    else if(digit==2) {p=two;n=sizeof two/sizeof two[0];}
    else if(digit==1) {p=one;n=sizeof one/sizeof one[0];}
    else return;
    /* Late fade drops deterministic vector pieces, exposing the scene through
     * the numeral while keeping the GPU's ordinary opaque draw path. */
    stroke(p,n,inner,248,scale,fade,1);
    stroke(p,n,outer,8,scale,fade,2);
}

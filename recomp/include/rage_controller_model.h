#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace rage_controller {
struct Point {double x,y,z;};
struct Triangle {Point p[3];uint32_t colour;int layer=0;};
using Mesh=std::vector<Triangle>;
inline Point sub(Point a,Point b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Point cross(Point a,Point b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline void tri(Mesh& m,Point a,Point b,Point c,uint32_t colour){m.push_back({{a,b,c},colour});}
inline void quad(Mesh& m,Point a,Point b,Point c,Point d,uint32_t colour){tri(m,a,b,c,colour);tri(m,a,c,d,colour);}
inline void box(Mesh& m,double x,double y,double z,double w,double h,double depth,uint32_t colour){
    Point p[8];for(int i=0;i<8;++i)p[i]={x+((i&1)?w:-w)*.5,y+((i&2)?h:-h)*.5,z+((i&4)?depth:-depth)*.5};
    const int faces[][4]={{4,5,7,6},{1,0,2,3},{0,4,6,2},{5,1,3,7},{2,6,7,3},{0,1,5,4}};
    for(const auto& f:faces)quad(m,p[f[0]],p[f[1]],p[f[2]],p[f[3]],colour);
}
inline void cylinder(Mesh& m,double x,double y,double z,double radius,double height,uint32_t colour,int sides=12){
    constexpr double tau=6.283185307179586;
    for(int i=0;i<sides;++i){const double a=tau*i/sides,b=tau*(i+1)/sides;
        Point p{x+radius*std::cos(a),y+radius*std::sin(a),z},q{x+radius*std::cos(b),y+radius*std::sin(b),z};
        Point r{q.x,q.y,z+height},s{p.x,p.y,z+height};
        quad(m,p,q,r,s,colour);tri(m,{x,y,z+height},s,r,colour);
    }
}
inline void stroke(Mesh& m,double x,double y,double xx,double yy,double z,double width,uint32_t colour){
    const double dx=xx-x,dy=yy-y,length=std::hypot(dx,dy);if(length<.001)return;
    const double nx=-dy*width/(2*length),ny=dx*width/(2*length);
    quad(m,{x-nx,y-ny,z},{xx-nx,yy-ny,z},{xx+nx,yy+ny,z},{x+nx,y+ny,z},colour);
}
inline double area(Point a,Point b,Point c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
inline void cap(Mesh& m,const std::vector<Point>& points,uint32_t colour,bool back=false){
    std::vector<unsigned> polygon;for(unsigned i=0;i<points.size();++i)polygon.push_back(i);
    while(polygon.size()>2){bool clipped=false;
        for(size_t i=0;i<polygon.size();++i){const size_t prev=(i+polygon.size()-1)%polygon.size(),next=(i+1)%polygon.size();
            const auto a=points[polygon[prev]],b=points[polygon[i]],c=points[polygon[next]];if(area(a,b,c)<=.0001)continue;
            bool inside=false;for(auto id:polygon){if(id==polygon[prev] || id==polygon[i] || id==polygon[next])continue;
                const auto p=points[id];if(area(a,b,p)>=0 && area(b,c,p)>=0 && area(c,a,p)>=0){inside=true;break;}}
            if(inside)continue;
            if(back)tri(m,c,b,a,colour);else tri(m,a,b,c,colour);
            polygon.erase(polygon.begin()+i);clipped=true;break;
        }
        if(!clipped)break;
    }
}
inline Mesh build(){
    Mesh m;
    // Original digital PlayStation pad: compact grips and an uninterrupted centre bridge.
    std::vector<Point> outline={{-61,22,0},{-56,35,0},{-41,40,0},{-26,36,0},{-18,29,0},
        {18,29,0},{26,36,0},{41,40,0},{56,35,0},{61,22,0},{62,5,0},{67,-24,0},
        {64,-37,0},{55,-43,0},{45,-40,0},{31,-18,0},{24,-6,0},{-24,-6,0},
        {-31,-18,0},{-45,-40,0},{-55,-43,0},{-64,-37,0},{-67,-24,0},{-62,5,0}};
    double signed_area=0;for(size_t i=0;i<outline.size();++i){const auto a=outline[i],b=outline[(i+1)%outline.size()];signed_area+=a.x*b.y-a.y*b.x;}
    if(signed_area<0)std::reverse(outline.begin(),outline.end());
    std::vector<Point> rounded;
    for(size_t i=0;i<outline.size();++i){auto a=outline[i],b=outline[(i+1)%outline.size()];
        rounded.push_back({a.x*.75+b.x*.25,a.y*.75+b.y*.25,0});
        rounded.push_back({a.x*.25+b.x*.75,a.y*.25+b.y*.75,0});}
    outline=std::move(rounded);
    const double scales[]={.92,1.,1.,.96},zs[]={-10,-6,6,10};
    std::vector<Point> rings[4];for(int r=0;r<4;++r)for(auto p:outline)rings[r].push_back({p.x*scales[r],p.y*scales[r],zs[r]});
    cap(m,rings[3],0xbcc0c3);cap(m,rings[0],0x969a9d,true);
    for(int r=0;r<3;++r)for(size_t i=0;i<outline.size();++i){const auto j=(i+1)%outline.size();
        quad(m,rings[r][i],rings[r][j],rings[r+1][j],rings[r+1][i],r==2?0xd0d3d5:0xa4a8ab);}
    const size_t shell_faces=m.size();
    // Shoulder controls sit on the rear/top edge, behind the front shell.
    // Keep them in a rear layer so painter sorting cannot expose their buried bases.
    const size_t shoulder_first=m.size();
    for(int side:{-1,1}){
        box(m,side*37,38,2,24,8,8,0x292724); // L1/R1 shoulders.
        box(m,side*39,40,-6,25,8,6,0x1e1c1a); // L2/R2 shoulders.
    }
    for(size_t f=shoulder_first;f<m.size();++f)m[f].layer=-1;
    cylinder(m,-38,16,10,17,1.2,0x9fa3a6,20);
    // Four separate directional keys, with a recessed centre.
    for(int side:{-1,1}){
        box(m,-38+side*8,16,14,8,7,6,0x242220);
        box(m,-38,16+side*8,14,7,8,6,0x242220);
    }
    box(m,-38,16,12,5,5,2,0x1d1b19);
    const double buttons[4][2]={{38,28},{50,16},{38,4},{26,16}};
    for(int i=0;i<4;++i){const auto x=buttons[i][0],y=buttons[i][1];
        cylinder(m,x,y,10,5.4,5,0x262422,12);
        const size_t icon_first=m.size();
        const double z=15.4;
        if(i==0){stroke(m,x,y+3,x-3,y-2,z,.8,0x8bbf66);stroke(m,x-3,y-2,x+3,y-2,z,.8,0x8bbf66);stroke(m,x+3,y-2,x,y+3,z,.8,0x8bbf66);}
        if(i==1)for(int j=0;j<16;++j){double a=j*6.283185307179586/16,b=(j+1)*6.283185307179586/16;
            stroke(m,x+2.7*std::cos(a),y+2.7*std::sin(a),x+2.7*std::cos(b),y+2.7*std::sin(b),z,.8,0x6262ce);}
        if(i==2){stroke(m,x-2.5,y-2.5,x+2.5,y+2.5,z,.8,0xcf9563);stroke(m,x-2.5,y+2.5,x+2.5,y-2.5,z,.8,0xcf9563);}
        if(i==3){stroke(m,x-2.5,y-2.5,x+2.5,y-2.5,z,.8,0xc27db9);stroke(m,x+2.5,y-2.5,x+2.5,y+2.5,z,.8,0xc27db9);
            stroke(m,x+2.5,y+2.5,x-2.5,y+2.5,z,.8,0xc27db9);stroke(m,x-2.5,y+2.5,x-2.5,y-2.5,z,.8,0xc27db9);}
        for(size_t f=icon_first;f<m.size();++f)m[f].layer=2;
    }
    box(m,-9,15,12,9,3.2,3,0x252321); // Select.
    tri(m,{6,12.5,13.5},{13,15,13.5},{6,17.5,13.5},0x302d29); // Start.
    box(m,0,23,10.5,15,2,.5,0x65696c); // Subtle manufacturer's plaque.
    // Cable strain relief and a curved cable exiting the top of the shell.
    const size_t cable_first=m.size();
    box(m,0,32,-6,5,10,7,0x242220);
    const double cable[][2]={{0,35},{0,46},{4,57},{13,63},{20,72},{18,83}};
    for(int i=0;i<5;++i)stroke(m,cable[i][0],cable[i][1],cable[i+1][0],cable[i+1][1],-8,2.2,0x45413d);
    // Rear cable geometry must remain behind the shell in the painter renderer.
    for(size_t f=cable_first;f<m.size();++f)m[f].layer=-1;
    // The display never turns the face away. Draw raised controls after the
    // shell so large concave-shell triangles cannot cover a small control
    // through a mean-depth approximation in the original painter renderer.
    for(size_t f=shell_faces;f<m.size();++f)if(!m[f].layer)m[f].layer=1;
    return m;
}
}

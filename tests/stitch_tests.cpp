#include "rage_track.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
#define require(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
static RageTrackCourse fixture(float gap=1){
 RageTrackCourse m;m.vertices={{{0,0,0}},{{100,0,0}},{{0,0,100}},{{100+gap,0,0}},{{100,0,100}},{{gap,0,100}}};
 RageTrackFace a{},b{};a.vertex[0]=0;a.vertex[1]=1;a.vertex[2]=a.vertex[3]=2;a.first_triangle=0;a.triangle_count=1;
 b.vertex[0]=3;b.vertex[1]=4;b.vertex[2]=b.vertex[3]=5;b.first_triangle=1;b.triangle_count=1;m.faces={a,b};
 m.triangles={{{0,1,2},{0,0,100,0,0,100}},{{3,4,5},{8,0,80,70,0,80}}};return m;
}
int main(){try{
 auto m=fixture();const auto uv=m.triangles[1];require(rage_track_stitch_geometry(m)>0);
 require(m.triangles[0].vertex[1]==m.triangles[1].vertex[0]);require(m.triangles[0].vertex[2]==m.triangles[1].vertex[2]);
 require(!std::memcmp(uv.uv,m.triangles[1].uv,sizeof uv.uv));require(rage_track_stitch_geometry(m)==0);
 auto layers=fixture();layers.faces[1].decal=1;rage_track_stitch_geometry(layers);require(layers.vertices[3][0]==101);
 auto separate=fixture(5);rage_track_stitch_geometry(separate);require(separate.vertices[3][0]==105);
 auto t=fixture();t.vertices[3]={50,0,0};t.vertices[4]={50,0,-50};t.vertices[5]={0,0,0};
 rage_track_stitch_geometry(t);require(t.faces[0].triangle_count>1);
 for(const auto& tri:t.triangles)for(auto id:tri.vertex)require(id<t.vertices.size());
 auto near=fixture();near.vertices={{{0,0,0}},{{100,0,0}},{{0,0,100}},{{50,0,1}},{{50,0,-50}}};
 RageTrackFace a=near.faces[0],b=near.faces[1],c=b;b.first_triangle=1;c.first_triangle=2;a.first_triangle=0;
 b.vertex[0]=3;b.vertex[1]=0;b.vertex[2]=b.vertex[3]=4;c.vertex[0]=1;c.vertex[1]=3;c.vertex[2]=c.vertex[3]=4;
 near.faces={a,b,c};near.triangles={{{0,1,2},{0,0,100,0,0,100}},{{3,0,4},{50,0,0,0,50,50}},{{1,3,4},{100,0,50,0,50,50}}};
 rage_track_stitch_geometry(near);require(near.faces[0].triangle_count>1);require(near.vertices[3][2]==1);

 std::cout<<"Seam positions, texture seams, raised layers and T junctions passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

#include "rage_track.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace {
struct Cell {int x,y,z;bool operator==(const Cell& c)const{return x==c.x && y==c.y && z==c.z;}};
struct Hash {size_t operator()(const Cell& c)const {
    uint64_t h=1469598103934665603ull;
    for(int v:{c.x,c.y,c.z})h=(h^uint32_t(v))*1099511628211ull;
    return size_t(h);
}};
Cell cell(const std::array<float,3>& p) {
    return {int(std::floor(p[0]/256)),int(std::floor(p[1]/256)),int(std::floor(p[2]/256))};
}
struct Corner {uint32_t vertex;float u,v;};
}

uint32_t rage_track_classify_layers(RageTrackCourse& course) {
    struct Surface {double area=0;bool ground=false;float lo[3],hi[3];};
    std::vector<Surface> surfaces(course.faces.size());
    std::unordered_map<Cell,std::vector<uint32_t>,Hash> bins;
    const int triangles[2][3]={{0,1,2},{2,1,3}};
    auto contains=[&](const RageTrackFace& face,const std::array<float,3>& p) {
        for(const auto& tri:triangles){
            const auto& a=course.vertices[face.vertex[tri[0]]];
            const auto& b=course.vertices[face.vertex[tri[1]]];
            const auto& c=course.vertices[face.vertex[tri[2]]];
            double e[3],f[3],v[3],n[3];for(int k=0;k<3;++k){e[k]=b[k]-a[k];f[k]=c[k]-a[k];v[k]=p[k]-a[k];}
            for(int k=0;k<3;++k)n[k]=e[(k+1)%3]*f[(k+2)%3]-e[(k+2)%3]*f[(k+1)%3];
            double nn=0,d=0,ee=0,ef=0,ff=0,ve=0,vf=0;
            for(int k=0;k<3;++k){nn+=n[k]*n[k];d+=v[k]*n[k];ee+=e[k]*e[k];ef+=e[k]*f[k];ff+=f[k]*f[k];ve+=v[k]*e[k];vf+=v[k]*f[k];}
            const double det=ee*ff-ef*ef;if(det<1e-8 || d*d>nn*0.25*0.25)continue;
            const double u=(ff*ve-ef*vf)/det,w=(ee*vf-ef*ve)/det;
            if(u>=-1e-6 && w>=-1e-6 && u+w<=1.000001)return true;
        }
        return false;
    };
    for(uint32_t i=0;i<course.faces.size();++i){
        auto& face=course.faces[i];face.decal=0;auto& s=surfaces[i];
        for(int k=0;k<3;++k){s.lo[k]=1e30f;s.hi[k]=-1e30f;}
        for(uint32_t vi:face.vertex){if(vi>=course.vertices.size())return 0;
            for(int k=0;k<3;++k){s.lo[k]=std::min(s.lo[k],course.vertices[vi][k]);s.hi[k]=std::max(s.hi[k],course.vertices[vi][k]);}}
        for(const auto& tri:triangles){
            const auto& a=course.vertices[face.vertex[tri[0]]];const auto& b=course.vertices[face.vertex[tri[1]]];const auto& c=course.vertices[face.vertex[tri[2]]];
            double e[3],f[3],n[3],nn=0;for(int k=0;k<3;++k){e[k]=b[k]-a[k];f[k]=c[k]-a[k];}
            for(int k=0;k<3;++k){n[k]=e[(k+1)%3]*f[(k+2)%3]-e[(k+2)%3]*f[(k+1)%3];nn+=n[k]*n[k];}
            s.area+=std::sqrt(nn)*0.5;s.ground|=n[1]*n[1]>nn*0.5 && nn>1e-8;
        }
        if(!s.ground)continue;
        const int x0=int(std::floor(s.lo[0]/512)),x1=int(std::floor(s.hi[0]/512));
        const int z0=int(std::floor(s.lo[2]/512)),z1=int(std::floor(s.hi[2]/512));
        if(int64_t(x1-x0+1)*(z1-z0+1)>1024)continue;
        for(int x=x0;x<=x1;++x)for(int z=z0;z<=z1;++z)bins[{x,0,z}].push_back(i);
    }
    uint32_t details=0;
    for(uint32_t i=0;i<course.faces.size();++i){
        auto& face=course.faces[i];const auto& s=surfaces[i];if(!s.ground)continue;
        Cell key{int(std::floor((s.lo[0]+s.hi[0])/1024)),0,int(std::floor((s.lo[2]+s.hi[2])/1024))};
        auto found=bins.find(key);if(found==bins.end())continue;
        for(uint32_t j:found->second){const auto& base=course.faces[j];const auto& b=surfaces[j];
            if(i==j || b.area<s.area*1.5 || (base.page==face.page && base.clut==face.clut && base.colour==face.colour &&
                base.window==face.window && base.type==face.type && !std::memcmp(base.uv,face.uv,sizeof face.uv)))continue;
            bool inside=true;for(int k=0;k<3;++k)inside&=s.lo[k]>=b.lo[k]-0.25f && s.hi[k]<=b.hi[k]+0.25f;
            if(!inside)continue;
            for(uint32_t v:face.vertex)inside&=contains(base,course.vertices[v]);
            if(inside){face.decal=1;++details;break;}
        }
    }
    return details;
}

// Split narrow ground overlays at their receivers' triangle boundaries. The
// original painter-rendered strips can span several quads, including twisted
// road quads; classifying by containment in one face misses those strips.
uint32_t rage_track_conform_details(RageTrackCourse& course) {
    struct Surface {double area=0;bool ground=false,narrow=false;float lo[3],hi[3];};
    struct V {double x,y,z,u,v;};
    const int indices[2][3]={{0,1,2},{2,1,3}};
    std::vector<Surface> surfaces(course.faces.size());
    std::unordered_map<Cell,std::vector<uint32_t>,Hash> bins;
    for(uint32_t i=0;i<course.faces.size();++i){
        const auto& f=course.faces[i];auto& s=surfaces[i];double longest=0,ny=0,nn=0;
        for(int k=0;k<3;++k){s.lo[k]=1e30f;s.hi[k]=-1e30f;}
        for(int k=0;k<4;++k){const auto& p=course.vertices[f.vertex[k]];
            for(int j=0;j<3;++j){s.lo[j]=std::min(s.lo[j],p[j]);s.hi[j]=std::max(s.hi[j],p[j]);}
            for(int l=0;l<k;++l){double d=0;for(int j=0;j<3;++j)d+=std::pow(double(p[j])-course.vertices[f.vertex[l]][j],2);longest=std::max(longest,d);}}
        for(const auto& t:indices){const auto& a=course.vertices[f.vertex[t[0]]];const auto& b=course.vertices[f.vertex[t[1]]];const auto& c=course.vertices[f.vertex[t[2]]];
            double e[3],g[3],n[3];for(int k=0;k<3;++k){e[k]=b[k]-a[k];g[k]=c[k]-a[k];}
            double n2=0;for(int k=0;k<3;++k){n[k]=e[(k+1)%3]*g[(k+2)%3]-e[(k+2)%3]*g[(k+1)%3];n2+=n[k]*n[k];}
            s.area+=std::sqrt(n2)*.5;ny+=std::abs(n[1]);nn+=std::sqrt(n2);}
        s.ground=nn>1e-8 && ny>nn*.707;s.narrow=s.ground && longest>0 && s.area*2/longest<.08;
        if(!s.ground || s.narrow || f.decal)continue;
        int x0=int(std::floor(s.lo[0]/512)),x1=int(std::floor(s.hi[0]/512)),z0=int(std::floor(s.lo[2]/512)),z1=int(std::floor(s.hi[2]/512));
        if(int64_t(x1-x0+1)*(z1-z0+1)>1024)continue;
        for(int x=x0;x<=x1;++x)for(int z=z0;z<=z1;++z)bins[{x,0,z}].push_back(i);
    }
    auto cross=[](const V& a,const V& b,const V& c){return (b.x-a.x)*(c.z-a.z)-(b.z-a.z)*(c.x-a.x);};
    auto area=[&](const std::vector<V>& p){double a=0;for(size_t k=1;k+1<p.size();++k)a+=std::abs(cross(p[0],p[k],p[k+1]))*.5;return a;};
    auto lerp=[](const V& a,const V& b,double t){return V{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.u+(b.u-a.u)*t,a.v+(b.v-a.v)*t};};
    auto point=[&](uint32_t id){const auto& p=course.vertices[id];return V{p[0],p[1],p[2],0,0};};
    auto height=[&](const V r[3],const V& p){const double d=cross(r[0],r[1],r[2]);return r[0].y+(r[1].y-r[0].y)*cross(r[0],p,r[2])/d+(r[2].y-r[0].y)*cross(r[0],r[1],p)/d;};
    const auto original=course.triangles;
    std::vector<RageTrackTriangle> rebuilt;rebuilt.reserve(original.size());uint32_t conformed=0;
    std::unordered_map<uint64_t,unsigned> proven_materials;
    for(uint32_t i=0;i<course.faces.size();++i){
        auto& f=course.faces[i];const auto& s=surfaces[i];std::vector<std::vector<V>> output;
        const uint64_t material=uint64_t(f.page)|(uint64_t(f.clut)<<16)|(uint64_t(f.window)<<32);
        // Several tightly supported strips establish an authored detail
        // material. Other strips of that material may have larger authored
        // clearance on banked roads; still require a nearby receiver.
        const double support_tolerance=proven_materials[material]>=4?16:2;
        if(s.narrow || f.decal || (s.ground && f.depth_bias<0)){
            std::vector<uint32_t> candidates;std::unordered_set<uint32_t> found;
            const int x0=int(std::floor(s.lo[0]/512)),x1=int(std::floor(s.hi[0]/512)),z0=int(std::floor(s.lo[2]/512)),z1=int(std::floor(s.hi[2]/512));
            if(int64_t(x1-x0+1)*(z1-z0+1)<=1024)for(int x=x0;x<=x1;++x)for(int z=z0;z<=z1;++z){
                auto bin=bins.find({x,0,z});if(bin==bins.end())continue;
                for(uint32_t j:bin->second){const auto& b=course.faces[j];const auto& bs=surfaces[j];
                    if(!found.insert(j).second || bs.area<s.area*1.5 || bs.lo[1]>s.hi[1]+32 || bs.hi[1]<s.lo[1]-32 ||
                       (b.page==f.page && b.clut==f.clut && b.type==f.type && b.window==f.window))continue;
                    candidates.push_back(j);}}
            std::sort(candidates.begin(),candidates.end());
            // Require every authored corner to meet a nearby receiver.
            // Isolated raised strips/bridges are not ground decals.
            bool supported=!candidates.empty() && candidates.size()<=256;
            for(int k=0;k<4 && supported;++k){V p=point(f.vertex[k]);bool hit=false;
                for(uint32_t j:candidates)for(const auto& t:indices){const auto& b=course.faces[j];V r[3]={point(b.vertex[t[0]]),point(b.vertex[t[1]]),point(b.vertex[t[2]])};
                    const double d=cross(r[0],r[1],r[2]);if(std::abs(d)<1e-8)continue;
                    const double u=cross(r[0],p,r[2])/d,v=cross(r[0],r[1],p)/d;
                    if(u>=-.002 && v>=-.002 && u+v<=1.002 && std::abs(p.y-height(r,p))<=support_tolerance)hit=true;}
                supported=hit;}
            if(supported){
                double expected=0,covered=0;
                for(const auto& t:indices){std::vector<V> start;
                    for(int k:t){V p=point(f.vertex[k]);p.u=f.uv[k]&255;p.v=(f.uv[k]>>8)&255;start.push_back(p);}expected+=area(start);
                    std::vector<std::vector<V>> remaining{start};
                    for(uint32_t j:candidates)for(const auto& rt:indices){
                        const auto& b=course.faces[j];V r[3]={point(b.vertex[rt[0]]),point(b.vertex[rt[1]]),point(b.vertex[rt[2]])};
                        const double det=cross(r[0],r[1],r[2]);if(std::abs(det)<1e-8)continue;
                        std::vector<std::vector<V>> next;
                        for(const auto& source:remaining){
                            std::vector<V> inside=source;std::vector<std::vector<V>> outside;
                            for(int edge=0;edge<3 && !inside.empty();++edge){std::vector<V> in,out;
                                for(size_t k=0;k<inside.size();++k){const V a=inside[k],b=inside[(k+1)%inside.size()];
                                    const double da=cross(r[edge],r[(edge+1)%3],a)*(det>0?1:-1),db=cross(r[edge],r[(edge+1)%3],b)*(det>0?1:-1);
                                    if(da>=0)in.push_back(a);else out.push_back(a);
                                    if((da>=0)!=(db>=0)){V p=lerp(a,b,da/(da-db));in.push_back(p);out.push_back(p);}}
                                if(out.size()>=3 && area(out)>1e-5)outside.push_back(std::move(out));inside=std::move(in);}
                            bool fits=inside.size()>=3 && area(inside)>1e-5;
                            if(fits)for(const auto& p:inside)if(std::abs(p.y-height(r,p))>32)fits=false;
                            if(fits){covered+=area(inside);for(auto& p:inside)p.y=height(r,p);output.push_back(std::move(inside));
                                for(auto& p:outside)next.push_back(std::move(p));}
                            else next.push_back(source);
                        }
                        remaining=std::move(next);
                    }
                    // Integer authoring may put a strip edge fractionally beyond
                    // a road boundary. Keep those thin slivers on the nearest
                    // receiver's plane instead of dropping or widening them.
                    for(auto& fragment:remaining){bool fitted=false;
                        for(uint32_t j:candidates){if(fitted)break;for(const auto& rt:indices){
                            const auto& b=course.faces[j];V r[3]={point(b.vertex[rt[0]]),point(b.vertex[rt[1]]),point(b.vertex[rt[2]])};
                            const double det=cross(r[0],r[1],r[2]);if(std::abs(det)<1e-8)continue;bool fits=true;
                            for(const auto& p:fragment){if(std::abs(p.y-height(r,p))>32){fits=false;break;}
                                for(int edge=0;edge<3;++edge){const V a=r[edge],b=r[(edge+1)%3];const double length=std::hypot(b.x-a.x,b.z-a.z);
                                    if(cross(a,b,p)*(det>0?1:-1)<-support_tolerance*length){fits=false;break;}}}
                            if(fits){covered+=area(fragment);for(auto& p:fragment)p.y=height(r,p);output.push_back(std::move(fragment));fitted=true;break;}
                        }}
                    }
                }
                // Do not replace an incompletely supported overlay with holes.
                if(expected<1e-6 || covered<expected*.9999)output.clear();
            }
        }
        const uint32_t old_first=f.first_triangle,old_count=f.triangle_count;f.first_triangle=(uint32_t)rebuilt.size();
        size_t extra_vertices=0,extra_triangles=0;
        for(const auto& p:output){extra_vertices+=p.size();extra_triangles+=p.size()-2;}
        if(course.vertices.size()+extra_vertices>262144 || rebuilt.size()+extra_triangles+original.size()-(old_first+old_count)>262144)output.clear();
        if(!output.empty()){
            f.decal=1;++conformed;if(support_tolerance==2)++proven_materials[material];
            for(const auto& p:output){uint32_t first=(uint32_t)course.vertices.size();
                for(const auto& v:p)course.vertices.push_back({float(v.x),float(v.y),float(v.z)});
                for(size_t k=1;k+1<p.size();++k){if(std::abs(cross(p[0],p[k],p[k+1]))<1e-5)continue;
                    rebuilt.push_back({{first,first+(uint32_t)k,first+(uint32_t)k+1},{float(p[0].u),float(p[0].v),float(p[k].u),float(p[k].v),float(p[k+1].u),float(p[k+1].v)}});}}
        } else for(uint32_t k=old_first;k<old_first+old_count;++k)rebuilt.push_back(original[k]);
        f.triangle_count=(uint32_t)rebuilt.size()-f.first_triangle;
    }
    course.triangles=std::move(rebuilt);return conformed;
}

bool rage_track_build_topology(RageTrackCourse& course) {
    // Repair exact T-junctions by splitting the coarse edge at the existing
    // shared vertex. Original positions stay unchanged; no triangle inflation.
    if(course.source_vertices && course.source_vertices<=course.vertices.size())course.vertices.resize(course.source_vertices);
    if(course.vertices.size()>262144 || course.faces.size()>65536)return false;
    course.triangles.clear();course.tjunctions=0;
    course.source_vertices=(uint32_t)course.vertices.size();
    std::unordered_map<Cell,std::vector<uint32_t>,Hash> grid;
    for(uint32_t i=0;i<course.source_vertices;++i)grid[cell(course.vertices[i])].push_back(i);
    for(auto& face:course.faces) {
        face.first_triangle=(uint32_t)course.triangles.size();
        const int indices[2][3]={{0,1,2},{2,1,3}};
        for(const auto& tri:indices) {
            Corner original[3];
            for(int k=0;k<3;++k)original[k]={face.vertex[tri[k]],float(face.uv[tri[k]]&255),float((face.uv[tri[k]]>>8)&255)};
            std::vector<Corner> boundary;
            for(int edge=0;edge<3;++edge) {
                const auto& a=original[edge];const auto& b=original[(edge+1)%3];boundary.push_back(a);
                const auto p=course.vertices[a.vertex],q=course.vertices[b.vertex];
                double delta[3],length=0,span=0;
                for(int k=0;k<3;++k){delta[k]=double(q[k])-p[k];length+=delta[k]*delta[k];span=std::max(span,std::abs(delta[k]));}
                if(length<1e-12)continue;
                std::unordered_set<uint32_t> visited;
                std::vector<std::pair<double,uint32_t>> splits;
                const unsigned steps=std::max(1u,(unsigned)std::ceil(span/128));
                for(unsigned step=0;step<=steps;++step) {
                    std::array<float,3> point;
                    for(int k=0;k<3;++k)point[k]=(float)(p[k]+delta[k]*step/steps);
                    const Cell base=cell(point);
                    // Half-cell stepping and neighbour cells cover the whole edge,
                    // including points lying exactly on a spatial bin boundary.
                    for(int z=-1;z<=1;++z)for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
                        auto bucket=grid.find({base.x+x,base.y+y,base.z+z});if(bucket==grid.end())continue;
                        for(uint32_t id:bucket->second) {
                            if(id==a.vertex || id==b.vertex || !visited.insert(id).second)continue;
                            const auto& v=course.vertices[id];double t=0;
                            for(int k=0;k<3;++k)t+=(double(v[k])-p[k])*delta[k];t/=length;
                            if(t<=1e-8 || t>=1-1e-8)continue;
                            double distance=0;for(int k=0;k<3;++k){const double d=v[k]-(p[k]+t*delta[k]);distance+=d*d;}
                            if(distance<=1e-8)splits.emplace_back(t,id);
                        }
                    }
                }
                std::sort(splits.begin(),splits.end());
                for(const auto& split:splits)boundary.push_back({split.second,
                    (float)(a.u+(b.u-a.u)*split.first),(float)(a.v+(b.v-a.v)*split.first)});
                course.tjunctions+=(uint32_t)splits.size();
            }
            if(boundary.size()==3) {
                RageTrackTriangle result{};
                for(int k=0;k<3;++k){result.vertex[k]=original[k].vertex;result.uv[2*k]=original[k].u;result.uv[2*k+1]=original[k].v;}
                if(course.triangles.size()>=262144)return false;
                course.triangles.push_back(result);
            } else {
                std::array<float,3> centre{};float u=0,v=0;
                for(int k=0;k<3;++k){for(int j=0;j<3;++j)centre[j]+=course.vertices[original[k].vertex][j]/3;
                    u+=original[k].u/3;v+=original[k].v/3;}
                const uint32_t mid=(uint32_t)course.vertices.size();course.vertices.push_back(centre);
                for(size_t k=0;k<boundary.size();++k){const auto& a=boundary[k];const auto& b=boundary[(k+1)%boundary.size()];
                    if(course.triangles.size()>=262144)return false;
                    course.triangles.push_back({{mid,a.vertex,b.vertex},{u,v,a.u,a.v,b.u,b.v}});
                }
            }
        }
        face.triangle_count=(uint32_t)course.triangles.size()-face.first_triangle;
    }
    return true;
}

// Query the same opaque native triangles used for road depth, rather than
// the original actor's independent flat-height reference.
bool rage_track_sample_ground(const RageTrackCourse& course,float x,float z,float near_y,float& height){
    double best=128;bool found=false;
    for(const auto& sector:course.sectors){
        if(x<sector.lo[0] || x>sector.hi[0] || z<sector.lo[2] || z>sector.hi[2])continue;
        for(uint32_t i=sector.first;i<sector.first+sector.count;++i){const auto& face=course.faces[i];if(face.decal)continue;
            for(uint32_t j=face.first_triangle;j<face.first_triangle+face.triangle_count;++j){const auto& t=course.triangles[j];
                const auto& a=course.vertices[t.vertex[0]];const auto& b=course.vertices[t.vertex[1]];const auto& c=course.vertices[t.vertex[2]];
                const double ex=b[0]-a[0],ez=b[2]-a[2],ey=b[1]-a[1],fx=c[0]-a[0],fz=c[2]-a[2],fy=c[1]-a[1];
                const double d=ex*fz-ez*fx,nx=ey*fz-ez*fy,nz=ex*fy-ey*fx;
                if(d*d<1 || d*d<nx*nx+nz*nz)continue;
                const double u=((x-a[0])*fz-(z-a[2])*fx)/d,v=(ex*(z-a[2])-ez*(x-a[0]))/d;
                if(u<0 || v<0 || u+v>1)continue;
                const double y=a[1]+u*ey+v*fy,dist=std::abs(y-near_y);
                if(dist<best){height=float(y);best=dist;found=true;}
            }
        }
    }
    return found;
}

// Weld matching open edges without touching UVs, decals or deliberate layers.
// Positions are shared; texture seams remain per-triangle attributes.
uint32_t rage_track_stitch_geometry(RageTrackCourse& m){
    struct Edge {uint32_t a,b,face;double n[3];};
    std::vector<uint32_t> parent(m.vertices.size());
    for(uint32_t i=0;i<parent.size();++i)parent[i]=i;
    auto root=[&](uint32_t i){while(parent[i]!=i){parent[i]=parent[parent[i]];i=parent[i];}return i;};
    auto join=[&](uint32_t a,uint32_t b){a=root(a);b=root(b);if(a!=b)parent[std::max(a,b)]=std::min(a,b);};
    struct PositionHash {size_t operator()(const std::array<float,3>& p)const{size_t h=0;for(float x:p)h^=std::hash<float>{}(x)+0x9e3779b9+(h<<6)+(h>>2);return h;}};
    std::unordered_map<std::array<float,3>,uint32_t,PositionHash> exact;
    for(uint32_t i=0;i<m.vertices.size();++i){auto it=exact.emplace(m.vertices[i],i);if(!it.second)join(i,it.first->second);}
    for(auto& f:m.faces)for(auto& v:f.vertex)v=root(v);
    for(auto& t:m.triangles)for(auto& v:t.vertex)v=root(v);
    std::unordered_map<uint64_t,std::vector<Edge>> incidence;
    for(uint32_t fi=0;fi<m.faces.size();++fi){const auto& f=m.faces[fi];if(f.decal || f.depth_bias)continue;
        for(uint32_t j=f.first_triangle;j<f.first_triangle+f.triangle_count;++j){const auto& t=m.triangles[j];
            const auto& a=m.vertices[t.vertex[0]];const auto& b=m.vertices[t.vertex[1]];const auto& c=m.vertices[t.vertex[2]];
            double e[3],g[3],n[3],length=0;for(int k=0;k<3;++k){e[k]=b[k]-a[k];g[k]=c[k]-a[k];}
            for(int k=0;k<3;++k){n[k]=e[(k+1)%3]*g[(k+2)%3]-e[(k+2)%3]*g[(k+1)%3];length+=n[k]*n[k];}
            if(length<1)continue;length=std::sqrt(length);for(double& x:n)x/=length;
            for(int k=0;k<3;++k){uint32_t x=t.vertex[k],y=t.vertex[(k+1)%3];if(x==y)continue;
                const uint64_t key=(uint64_t(std::min(x,y))<<32)|std::max(x,y);Edge edge{x,y,fi,{n[0],n[1],n[2]}};incidence[key].push_back(edge);}
        }
    }
    std::vector<Edge> edges;for(const auto& entry:incidence)if(entry.second.size()==1)edges.push_back(entry.second[0]);
    for(auto& f:m.faces)f.coverage=0;
    for(const auto& e:edges)m.faces[e.face].coverage=1;
    auto bucket=[](const std::array<float,3>& p){return Cell{int(std::floor(p[0]/4)),int(std::floor(p[1]/4)),int(std::floor(p[2]/4))};};
    std::unordered_map<Cell,std::vector<uint32_t>,Hash> starts;
    for(uint32_t i=0;i<edges.size();++i)starts[bucket(m.vertices[edges[i].a])].push_back(i);
    auto distance=[&](uint32_t a,uint32_t b){double d=0;for(int k=0;k<3;++k){double x=m.vertices[a][k]-m.vertices[b][k];d+=x*x;}return d;};
    uint32_t repaired=0;
    for(uint32_t i=0;i<edges.size();++i){const auto& a=edges[i];if(distance(a.a,a.b)<256)continue;const Cell key=bucket(m.vertices[a.b]);
        for(int z=-1;z<=1;++z)for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){auto found=starts.find({key.x+x,key.y+y,key.z+z});if(found==starts.end())continue;
            for(uint32_t j:found->second){const auto& b=edges[j];if(j<=i || a.face==b.face)continue;
                double dot=0;for(int k=0;k<3;++k)dot+=a.n[k]*b.n[k];
                if(dot<.9 || distance(a.a,b.b)>4 || distance(a.b,b.a)>4)continue;
                join(a.a,b.b);join(a.b,b.a);++repaired;
            }
        }
    }
    for(uint32_t i=0;i<m.vertices.size();++i)m.vertices[i]=m.vertices[root(i)];
    for(auto& f:m.faces)for(auto& v:f.vertex)v=root(v);
    for(auto& t:m.triangles)for(auto& v:t.vertex)v=root(v);
    // The loader's original exact T-junction repair precedes clipped Oval
    // supplementation. Split any newly shared edge while preserving UVs.
    std::unordered_map<Cell,std::vector<uint32_t>,Hash> grid;
    std::vector<unsigned char> boundary(m.vertices.size());
    std::vector<std::vector<uint32_t>> incident(m.vertices.size());
    for(uint32_t i=0;i<edges.size();++i){const auto& e=edges[i];boundary[root(e.a)]=boundary[root(e.b)]=1;incident[root(e.a)].push_back(i);incident[root(e.b)].push_back(i);}
    for(uint32_t i=0;i<m.vertices.size();++i)if(root(i)==i && boundary[i])grid[cell(m.vertices[i])].push_back(i);
    std::vector<RageTrackTriangle> out;out.reserve(m.triangles.size());
    const auto old=m.triangles;
    for(auto& f:m.faces){const uint32_t first=f.first_triangle,count=f.triangle_count;f.first_triangle=(uint32_t)out.size();
        for(uint32_t j=first;j<first+count;++j){const auto& t=old[j];if(f.decal || f.depth_bias){out.push_back(t);continue;}
            std::vector<Corner> polygon;
            for(int edge=0;edge<3;++edge){const int next=(edge+1)%3;Corner a{t.vertex[edge],t.uv[2*edge],t.uv[2*edge+1]},b{t.vertex[next],t.uv[2*next],t.uv[2*next+1]};polygon.push_back(a);
                const auto& p=m.vertices[a.vertex];const auto& q=m.vertices[b.vertex];double d[3],nn=0,span=0;for(int k=0;k<3;++k){d[k]=q[k]-p[k];nn+=d[k]*d[k];span=std::max(span,std::abs(d[k]));}if(nn<1)continue;
                std::unordered_set<uint32_t> seen;std::vector<std::pair<double,uint32_t>> splits;
                const unsigned steps=std::max(1u,(unsigned)std::ceil(span/128));
                for(unsigned step=0;step<=steps;++step){std::array<float,3> pos;for(int k=0;k<3;++k)pos[k]=float(p[k]+d[k]*step/steps);const Cell key=cell(pos);
                    for(int z=-1;z<=1;++z)for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){auto found=grid.find({key.x+x,key.y+y,key.z+z});if(found==grid.end())continue;
                        for(uint32_t v:found->second){if(v==a.vertex || v==b.vertex || !seen.insert(v).second)continue;double along=0;for(int k=0;k<3;++k)along+=(m.vertices[v][k]-p[k])*d[k];along/=nn;if(along<=1e-7 || along>=1-1e-7)continue;
                            double error=0;for(int k=0;k<3;++k){double dx=m.vertices[v][k]-(p[k]+along*d[k]);error+=dx*dx;}
                            bool compatible=error<=.001;
                            if(!compatible && error<=4){
                                const auto& r=m.vertices[t.vertex[(edge+2)%3]];double qv[3],normal[3],nnorm=0;for(int k=0;k<3;++k)qv[k]=r[k]-p[k];
                                for(int k=0;k<3;++k){normal[k]=d[(k+1)%3]*qv[(k+2)%3]-d[(k+2)%3]*qv[(k+1)%3];nnorm+=normal[k]*normal[k];}
                                for(uint32_t peer:incident[v]){const auto& e=edges[peer];if(e.face==uint32_t(&f-m.faces.data()))continue;
                                    double dot=0;for(int k=0;k<3;++k)dot+=normal[k]*e.n[k];if(dot<.9*std::sqrt(nnorm))continue;
                                    const uint32_t other=root(e.a)==v?root(e.b):root(e.a);double w=0;for(int k=0;k<3;++k)w+=(m.vertices[other][k]-p[k])*d[k];w/=nn;
                                    double off=0;for(int k=0;k<3;++k){double dx=m.vertices[other][k]-(p[k]+w*d[k]);off+=dx*dx;}
                                    if(off<=4 && w>=-1e-7 && w<=1.0000001){compatible=true;break;}
                                }
                            }
                            // Use the existing boundary point itself. This
                            // fills only the tiny seam without shifting that
                            // point, smoothing the road, or moving its UV seam.
                            if(compatible)splits.emplace_back(along,v);
                        }
                    }
                }
                std::sort(splits.begin(),splits.end());for(const auto& v:splits)polygon.push_back({v.second,float(a.u+(b.u-a.u)*v.first),float(a.v+(b.v-a.v)*v.first)});
            }
            if(polygon.size()==3 || m.vertices.size()>=262144 || out.size()+polygon.size()+old.size()-j>=262144){out.push_back(t);continue;}
            std::array<float,3> centre{};float u=0,v=0;for(int k=0;k<3;++k){for(int axis=0;axis<3;++axis)centre[axis]+=m.vertices[t.vertex[k]][axis]/3;u+=t.uv[2*k]/3;v+=t.uv[2*k+1]/3;}
            const uint32_t id=(uint32_t)m.vertices.size();m.vertices.push_back(centre);for(size_t k=0;k<polygon.size();++k){const auto& a=polygon[k];const auto& b=polygon[(k+1)%polygon.size()];out.push_back({{id,a.vertex,b.vertex},{u,v,a.u,a.v,b.u,b.v}});}++repaired;
        }
        f.triangle_count=(uint32_t)out.size()-f.first_triangle;
    }
    m.triangles=std::move(out);
    for(auto& sector:m.sectors){sector.draw_order.clear();for(uint32_t i=sector.first;i<sector.first+sector.count;++i)sector.draw_order.push_back(i);
        std::stable_sort(sector.draw_order.begin(),sector.draw_order.end(),[&](uint32_t a,uint32_t b){return m.faces[a].coverage<m.faces[b].coverage;});}
    for(auto& s:m.sectors){for(int k=0;k<3;++k){s.lo[k]=1e30f;s.hi[k]=-1e30f;}for(uint32_t i=s.first;i<s.first+s.count;++i){const auto& f=m.faces[i];for(uint32_t j=f.first_triangle;j<f.first_triangle+f.triangle_count;++j)for(uint32_t v:m.triangles[j].vertex)for(int k=0;k<3;++k){s.lo[k]=std::min(s.lo[k],m.vertices[v][k]);s.hi[k]=std::max(s.hi[k],m.vertices[v][k]);}}}
    return repaired;
}

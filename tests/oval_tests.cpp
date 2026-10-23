// Exercise the importer and geometric subtraction with this user's extracted
// disc fixtures. These fixtures are never packaged in the source or executable.
#include "../recomp/src/rage_oval.cpp"
#include <iostream>
void rage_free_world_routes(const RageFreeRoutes&,const std::array<std::array<float,3>,4>&){}
#define require(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
int main(int argc,char** argv){try{
 // A foreign fence across the lane is omitted; an overhead bridge and
 // a fence outside the driving corridor remain scenery.
 std::vector<RageTrackRoutePoint> lane{{0,0,0,100,100},{0,0,1000,100,100}};
 require(blocks_route({Point{-120,-80,500},Point{120,-80,500},Point{-120,0,500},Point{120,0,500}},lane));
 require(!blocks_route({Point{-120,-2000,500},Point{120,-2000,500},Point{-120,-1900,500},Point{120,-1900,500}},lane));
 require(!blocks_route({Point{300,-80,500},Point{600,-80,500},Point{300,0,500},Point{600,0,500}},lane));
 require(argc==2);std::filesystem::path root=argv[1];
 // Subtract overlapping clips sequentially: union removal must not count
 // overlaps twice, and new vertices must keep interpolated texture coordinates.
 Polygon p{{{0,0,0},0,0},{{10,0,0},100,0},{{0,0,10},0,100}};
 Surface s;require(surface({Point{0,0,0},Point{5,0,0},Point{0,0,5}},s));
 auto cut=subtract(p,s);double total=0;
 for(const auto& part:cut)for(size_t i=1;i+1<part.size();++i)total+=std::abs(cross(part[0].p,part[i].p,part[i+1].p,1))/2;
 require(std::abs(total-37.5)<1e-8);
 for(const auto& part:cut)for(const auto& v:part){require(std::abs(v.u-v.p[0]*10)<1e-8);require(std::abs(v.v-v.p[2]*10)<1e-8);}
 decode(file(root/"OVAL5.2ND"));auto source=oval;textures(file(root/"OVAL5.1ST"));require(pixels.size()==1024*512);
 uint64_t textureHash=1469598103934665603ull;for(auto word:pixels)for(unsigned byte:{unsigned(word&255),unsigned(word>>8)})textureHash=(textureHash^byte)*1099511628211ull;
 require(textureHash==0x541904d39783c415ull); // Independent existing TIM replay, PAL user fixture.
 // Oval's static placement table contains only a disabled (-1) sentinel.
 auto pack=file(root/"OVAL5.2ND");auto offsets=chunks(pack);auto objects=offsets[8];
 require(u32(pack,objects)==1 && int16_t(u16(pack,objects+4))==-1);

 for(auto name:{"BIG","MID","HI","OVAL"})for(int variant=1;variant<=6;++variant){
  auto course=std::string(name)+std::to_string(variant);oval={};decode(file(root/(course+".2ND")));auto active=oval;oval=source;
  auto data=file(root/(course+".2ND"));auto offsets=chunks(data);size_t path=offsets[4];for(unsigned i=0;i<u32(data,path);++i){auto p=path+4+24*i;
   active.route.push_back({float(int32_t(u32(data,p))*4.0),float(int16_t(u16(data,p+8))*4),float(int32_t(u32(data,p+4))*4.0),float(u16(data,p+16)*4),float(u16(data,p+18)*4)});}

  require(rage_track_build_topology(active));rage_track_classify_layers(active);
  const unsigned stitched=rage_track_stitch_geometry(active);rage_track_conform_details(active);
  std::cout<<course<<" active seam repairs "<<stitched<<"\n";
  RageTrackCourse drawing;bool extended=rage_oval_extend(active,drawing);
  if(std::string(name)!="OVAL")require(extended);
  if(!extended){std::cout<<course<<" unchanged\n";continue;}
  require(drawing.triangles.size()>active.triangles.size());
  std::set<Key> keys;
  for(size_t i=active.triangles.size();i<drawing.triangles.size();++i){const auto& t=drawing.triangles[i];Key k;for(int j=0;j<3;++j)k[j]=point(drawing,t.vertex[j]);require(keys.insert(key(k[0],k[1],k[2])).second);Surface valid;require(surface(k,valid));}
  // Independently intersect output against authored active surfaces. Allow
  // only float-rounding dust (four square world units), never a visible patch.
  if(variant==1 && std::string(name)!="OVAL"){
   std::vector<Surface> receivers;
   for(const auto& f:active.faces)for(const auto& c:corners){Surface r;if(surface({point(active,f.vertex[c[0]]),point(active,f.vertex[c[1]]),point(active,f.vertex[c[2]])},r))receivers.push_back(r);}
   for(size_t i=active.triangles.size();i<drawing.triangles.size();++i){const auto& t=drawing.triangles[i];Polygon tri;
    for(int k=0;k<3;++k)tri.push_back({point(drawing,t.vertex[k]),0,0});
    for(const auto& r:receivers){bool separate=false;for(int j=0;j<3;++j){double lo=tri[0].p[j],hi=lo;for(const auto& v:tri){lo=std::min(lo,v.p[j]);hi=std::max(hi,v.p[j]);}separate|=hi<r.lo[j]-.251 || r.hi[j]<lo-.251;}if(separate)continue;bool coplanar=true;
     for(const auto& v:tri){double d=-r.d;for(int j=0;j<3;++j)d+=r.n[j]*v.p[j];if(std::abs(d)>.251){coplanar=false;break;}}
     if(!coplanar)continue;auto intersection=tri;double sign=cross(r.p[0],r.p[1],r.p[2],r.axis)>0?1:-1;
     for(int j=0;j<3 && !intersection.empty();++j)intersection=half(intersection,r.p[j],r.p[(j+1)%3],r.axis,sign,true);
     double area=0;for(size_t j=1;j+1<intersection.size();++j)area+=std::abs(cross(intersection[0].p,intersection[j].p,intersection[j+1].p,r.axis))/2;
     if(area>4){std::cerr<<course<<" residual overlap tri="<<i<<" points "<<tri[0].p[0]<<","<<tri[0].p[1]<<","<<tri[0].p[2]<<" / "<<tri[1].p[0]<<","<<tri[1].p[1]<<","<<tri[1].p[2]<<" / "<<tri[2].p[0]<<","<<tri[2].p[1]<<","<<tri[2].p[2]<<" receiver "<<r.p[0][0]<<","<<r.p[0][1]<<","<<r.p[0][2]<<" / "<<r.p[1][0]<<","<<r.p[1][1]<<","<<r.p[1][2]<<" / "<<r.p[2][0]<<","<<r.p[2][1]<<","<<r.p[2][2]<<" area "<<area<<'\n';return 1;}
    }
   }
  }
  std::cout<<course<<" added "<<drawing.triangles.size()-active.triangles.size()<<" triangles\n";
 }
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

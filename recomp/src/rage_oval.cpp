#include "rage_oval.h"
#include "track_texture_snapshot.h"
#include <cstring>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Bytes=std::vector<uint8_t>;
using Point=std::array<double,3>;
using Quad=std::array<Point,4>;
using Key=std::array<Point,3>;
RageTrackCourse oval;
std::vector<uint16_t> pixels;
RageTrackTextureSnapshot track_textures[24]{};
uint32_t u16(const Bytes& b,size_t p){if(p+2>b.size())throw std::runtime_error("short asset");return b[p]|unsigned(b[p+1])<<8;}
uint32_t u32(const Bytes& b,size_t p){return u16(b,p)|(u16(b,p+2)<<16);}
Bytes file(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return Bytes(std::istreambuf_iterator<char>(f),{});}
struct Disc {
 std::ifstream in;uint64_t bytes;unsigned stride=2352,offset=24;
 Disc(const std::filesystem::path& p):in(p,std::ios::binary),bytes(std::filesystem::file_size(p)){}
 Bytes read(uint32_t sector,uint32_t count){
  if(count>4*1024*1024)throw std::runtime_error("large asset");
  Bytes b(count);for(size_t n=0;n<count;){size_t k=std::min(size_t(2048),count-n);uint64_t p=uint64_t(sector++)*stride+offset;
   if(p>bytes || k>bytes-p)throw std::runtime_error("disc extent");in.seekg(p);if(!in.read((char*)b.data()+n,k))throw std::runtime_error("disc read");n+=k;}return b;
 }
};
std::vector<size_t> chunks(const Bytes& b){
 unsigned n=u32(b,0)/4;if(!n || n>64 || u32(b,0)!=n*4)throw std::runtime_error("pack table");
 std::vector<size_t> o;for(unsigned i=0;i<n;++i){auto p=u32(b,4*i);if(p>b.size() || (!o.empty() && p<o.back()))throw std::runtime_error("pack offset");o.push_back(p);}o.push_back(b.size());return o;
}
Key key(const Point& a,const Point& b,const Point& c){Key k{a,b,c};std::sort(k.begin(),k.end());return k;}
Point point(const RageTrackCourse& m,uint32_t i){const auto& p=m.vertices.at(i);return {p[0],p[1],p[2]};}
Quad shape(const RageTrackCourse& m,const RageTrackFace& f,Point& origin){Quad q;for(int i=0;i<4;++i)q[i]=point(m,f.vertex[i]);std::sort(q.begin(),q.end());origin=q[0];for(auto& p:q)for(int j=0;j<3;++j)p[j]-=origin[j];return q;}
constexpr int corners[2][3]={{0,1,2},{1,3,2}};
struct V {Point p;double u,v;};
using Polygon=std::vector<V>;
struct Surface {Key p;Point n,lo,hi;double d;int axis;};
bool surface(const Key& p,Surface& s){
 Point a,b;for(int j=0;j<3;++j){a[j]=p[1][j]-p[0][j];b[j]=p[2][j]-p[0][j];}
 s.p=p;for(int j=0;j<3;++j){s.lo[j]=s.hi[j]=p[0][j];for(int k=1;k<3;++k){s.lo[j]=std::min(s.lo[j],p[k][j]);s.hi[j]=std::max(s.hi[j],p[k][j]);}}double length=0;for(int j=0;j<3;++j){s.n[j]=a[(j+1)%3]*b[(j+2)%3]-a[(j+2)%3]*b[(j+1)%3];length+=s.n[j]*s.n[j];}
 if(length<1e-10)return false;length=std::sqrt(length);s.d=0;s.axis=0;
 for(int j=0;j<3;++j){s.n[j]/=length;s.d+=s.n[j]*p[0][j];if(std::abs(s.n[j])>std::abs(s.n[s.axis]))s.axis=j;}return true;
}
double cross(const Point& a,const Point& b,const Point& c,int axis){int x=(axis+1)%3,y=(axis+2)%3;return (b[x]-a[x])*(c[y]-a[y])-(b[y]-a[y])*(c[x]-a[x]);}
Polygon half(const Polygon& poly,const Point& a,const Point& b,int axis,double sign,bool inside){
 Polygon out;if(poly.empty())return out;V prev=poly.back();double dp=cross(a,b,prev.p,axis)*sign;
 for(const auto& cur:poly){double dc=cross(a,b,cur.p,axis)*sign;bool ip=inside?dp>=0:dp<=0,ic=inside?dc>=0:dc<=0;
  if(ip!=ic){double t=dp/(dp-dc);V v;for(int j=0;j<3;++j)v.p[j]=prev.p[j]+t*(cur.p[j]-prev.p[j]);v.u=prev.u+t*(cur.u-prev.u);v.v=prev.v+t*(cur.v-prev.v);out.push_back(v);}
  if(ic)out.push_back(cur);prev=cur;dp=dc;}return out;
}
// A convex triangle's difference is a disjoint set of convex pieces. Each
// boundary also interpolates UVs, so partial overlaps retain the authored art.
double poly_area(const Polygon& p,int axis){double a=0;for(size_t i=1;i+1<p.size();++i)a+=std::abs(cross(p[0].p,p[i].p,p[i+1].p,axis));return a*.5;}
std::vector<Polygon> subtract(Polygon poly,const Surface& s){
 std::vector<Polygon> out;double sign=cross(s.p[0],s.p[1],s.p[2],s.axis)>0?1:-1;
 auto intersection=poly;for(int i=0;i<3 && !intersection.empty();++i)intersection=half(intersection,s.p[i],s.p[(i+1)%3],s.axis,sign,true);
 if(poly_area(intersection,s.axis)<.001)return {std::move(poly)};
 for(int i=0;i<3 && !poly.empty();++i){auto outside=half(poly,s.p[i],s.p[(i+1)%3],s.axis,sign,false);if(poly_area(outside,s.axis)>.001)out.push_back(std::move(outside));poly=half(poly,s.p[i],s.p[(i+1)%3],s.axis,sign,true);}return out;
}
void decode(const Bytes& pack){
 auto o=chunks(pack);if(o.size()!=12)throw std::runtime_error("course chunks");
 Bytes b(pack.begin()+o[7],pack.begin()+o[8]);unsigned n=u32(b,0x1800),pool=0x1800+u32(b,0x1804);
 if(!n || n>1023 || pool>=b.size())throw std::runtime_error("course header");
 for(unsigned z=0;z<32;++z)for(unsigned x=0;x<32;++x){unsigned id=u16(b,2*((31-z)*32+x))&1023;if(id==1023)continue;if(id>=n)throw std::runtime_error("sector id");
  size_t p=0x1800+u32(b,0x1808+4*id);unsigned groups=0;
  while(u32(b,p)){if(++groups>8192)throw std::runtime_error("sector stream");unsigned type=u16(b,p),count=u16(b,p+2);p+=4;if(type>5 || !count || count>8192)throw std::runtime_error("course face");unsigned stride=type==1 || type==4 || type==5?36:32;
   for(unsigned f=0;f<count;++f,p+=stride){RageTrackFace face{};face.type=uint16_t(type);for(int k=0;k<4;++k){size_t vi=pool+8*u16(b,p+2*k);face.vertex[k]=(uint32_t)oval.vertices.size();oval.vertices.push_back({float(int16_t(u16(b,vi))+int(x*8192+4096)),float(int16_t(u16(b,vi+2))),float(int16_t(u16(b,vi+4))+int(z*8192+4096))});}
    auto a=u32(b,p+8),c=u32(b,p+12),d=u32(b,p+16);face.uv[0]=a&65535;face.uv[1]=c&65535;face.uv[2]=d&65535;face.uv[3]=d>>16;face.clut=uint16_t(a>>16);face.page=uint16_t(c>>16)|0x8000;face.depth_bias=int8_t(b.at(p+21));face.colour=u32(b,p+28);face.window=stride==36?u32(b,p+32)&0xfffff:0;oval.faces.push_back(face);
   }
  }
 }
 if(oval.faces.size()<1000 || oval.faces.size()>20000)throw std::runtime_error("course size");
}
void textures(const Bytes& b,std::vector<uint16_t>* alternate=nullptr){
 std::vector<uint16_t> before;
 pixels.assign(1024*512,0);auto o=chunks(b);unsigned uploads=0;
 for(size_t chunk=0;chunk+1<o.size();++chunk)for(size_t p=o[chunk];p+20<=o[chunk+1];p+=4){
  if(u32(b,p)!=16 || (u32(b,p+4)&~15u) || (u32(b,p+4)&7)>3)continue;
  size_t q=p+8;struct Rect{unsigned x,y,w,h;size_t src;};std::vector<Rect> rects;bool valid=true;
  for(unsigned block=0,blocks=(u32(b,p+4)&8)?2:1;block<blocks;++block){if(q+12>o[chunk+1]){valid=false;break;}auto len=u32(b,q);unsigned x=u16(b,q+4),y=u16(b,q+6),w=u16(b,q+8),h=u16(b,q+10);
   if(len<12 || q+len>o[chunk+1] || x+w>1024 || y+h>512 || uint64_t(w)*h*2>len-12){valid=false;break;}rects.push_back({x,y,w,h,q+12});q+=len;}
  if(!valid)continue;for(const auto& r:rects)for(unsigned y=0;y<r.h;++y)for(unsigned x=0;x<r.w;++x)pixels[(r.y+y)*1024+r.x+x]=uint16_t(u16(b,r.src+2*(y*r.w+x)));
  ++uploads;p=(q+3)&~size_t(3);p-=4;
 }
 if(uploads<5)throw std::runtime_error("course textures");
 if(alternate){
  // Replay just the first four chunks to recover the saved row image.
  Bytes first(b.begin(),b.begin()+o[4]);for(size_t i=4;i+1<o.size();++i){unsigned end=(unsigned)first.size();for(int k=0;k<4;++k)first[4*i+k]=uint8_t(end>>(8*k));}
  auto final=pixels;textures(first);before=std::move(pixels);pixels=std::move(final);
  alternate->resize(448*256);for(int y=0;y<256;++y)std::copy_n(before.data()+(256+y)*1024+576,448,alternate->data()+y*448);
 }

}
}

void rage_oval_prepare(const std::filesystem::path& cue,const std::filesystem::path& executable){
 try{
  auto text=file(cue);std::string c(text.begin(),text.end());auto first=c.find('"'),last=c.find('"',first+1);if(first==std::string::npos || last==std::string::npos)throw std::runtime_error("disc cue");
  Disc disc(cue.parent_path()/std::filesystem::u8path(c.substr(first+1,last-first-1)));auto pvd=disc.read(16,2048);
  if(std::string((char*)pvd.data()+1,5)!="CD001")throw std::runtime_error("disc filesystem");auto dir=disc.read(u32(pvd,158),u32(pvd,166));unsigned rage=0;
  for(size_t p=0;p<dir.size();){unsigned len=dir[p];if(!len){p=(p/2048+1)*2048;continue;}if(len<34 || p+len>dir.size() || p+33+dir[p+32]>dir.size())throw std::runtime_error("disc directory");std::string name((char*)dir.data()+p+33,dir[p+32]);if(name=="RAGE.BIN;1" || name=="RAGE.BIN")rage=u32(dir,p+2);p+=len;}
  if(!rage)throw std::runtime_error("disc container");auto exe=file(executable);if(exe.size()<2048)throw std::runtime_error("disc executable");
  // Discover the original path-pointer array from its first string, rather
  // than assuming PAL table addresses or entry numbers on another region.
  std::string all(exe.begin(),exe.end());auto logo=all.find("\\DATA\\LOGO.TMS");if(logo==std::string::npos)throw std::runtime_error("asset path names");uint32_t address=u32(exe,0x18)+uint32_t(logo)-2048;size_t table=0;
  for(size_t p=2048;p+135*4<exe.size();p+=4)if(u32(exe,p)==address){bool valid=true;for(int i=0;i<135;++i){uint64_t v=u32(exe,p+4*i);valid&=v>=u32(exe,0x18) && v-u32(exe,0x18)+2048<exe.size();}if(valid){table=p;break;}}
  if(!table)throw std::runtime_error("asset path table");auto index=disc.read(rage,2048);Bytes geometry,texture;
  for(int i=0;i<135;++i){size_t at=u32(exe,table+4*i)-u32(exe,0x18)+2048;auto end=all.find('\0',at);if(end==std::string::npos)throw std::runtime_error("asset name");auto name=all.substr(at,end-at);
   if(name.find("OVAL5.1ST")!=std::string::npos)texture=disc.read(rage+u32(index,8*i),u32(index,8*i+4));
   auto leaf=name.substr(name.find_last_of('\\')+1);
   if(leaf.size()>=7 && leaf.substr(leaf.size()-4)==".1ST"){
    auto stem=leaf.substr(0,leaf.size()-4);int kind=stem.rfind("BIG",0)==0?0:stem.rfind("MID",0)==0?1:stem.rfind("HI",0)==0?2:stem.rfind("OVAL",0)==0?3:-1;
    int variant=stem.back()-'1';if(kind>=0 && variant>=0 && variant<6){
     auto pack=disc.read(rage+u32(index,8*i),u32(index,8*i+4));std::vector<uint16_t> shadow;textures(pack,&shadow);uint8_t flags[256];std::memset(flags,1,sizeof flags);
     RageTrackTextureSource source{};source.context=&pixels;source.read=[](void* context,uint16_t* out,size_t count){auto& p=*(std::vector<uint16_t>*)context;if(p.size()!=count)return 0;std::memcpy(out,p.data(),count*2);return 1;};source.shadowRows=shadow.data();source.shadowBytes=shadow.size()*2;source.shadowPages=flags;source.shadowPageCount=256;
     if(!TrackTextureSnapshotAcquire(&track_textures[variant*4+kind],1,&source,0))throw std::runtime_error("retained texture source");
    }
   }
   if(name.find("OVAL5.2ND")!=std::string::npos)geometry=disc.read(rage+u32(index,8*i),u32(index,8*i+4));
  }
  decode(geometry);textures(texture);std::printf("Oval scenery: imported %zu faces from the user's disc\n",oval.faces.size());
 }catch(const std::exception& e){oval={};pixels.clear();std::fprintf(stderr,"Oval scenery unavailable: %s\n",e.what());}
}
extern "C" const uint16_t* rage_course_texture_pixels(unsigned source){
 if(source==64)return pixels.empty()?nullptr:pixels.data();
 if(source<2 || source>=50)return nullptr;
 return TrackTextureSnapshotSelectPage(&track_textures[(source-2)/2],(source-2)&1);
}
extern "C" const uint16_t* rage_oval_texture_pixels(){return pixels.empty()?nullptr:pixels.data();}

// The donor course closes exits that belong to a different racing route.
// Remove only added raised geometry inside the active course's lane corridor;
// shared surfaces and the active course's own barriers remain authoritative.
static bool blocks_route(const Quad& face,const std::vector<RageTrackRoutePoint>& route){
 if(route.size()<2)return false;
 double low=face[0][1],high=low;for(const auto& p:face){low=std::min(low,p[1]);high=std::max(high,p[1]);}

 for(size_t i=0;i<route.size();++i){const auto& a=route[i];const auto& b=route[(i+1)%route.size()];
  const double dx=b.x-a.x,dz=b.z-a.z,nn=dx*dx+dz*dz;if(nn<1 || nn>8192.*8192)continue;
  for(int e=0;e<4;++e){const int edge[4]={0,1,3,2};const auto& p=face[edge[e]];const auto& q=face[edge[(e+1)%4]];
   // Closest points between a quad edge and the route segment in the XZ plane.
   double candidates[5]={0,1,0,0,0};int count=2;const double ex=q[0]-p[0],ez=q[2]-p[2],ee=ex*ex+ez*ez;
   if(ee>1){candidates[count++]=std::clamp(((a.x-p[0])*ex+(a.z-p[2])*ez)/ee,0.,1.);candidates[count++]=std::clamp(((b.x-p[0])*ex+(b.z-p[2])*ez)/ee,0.,1.);}
   const double det=ex*dz-ez*dx;if(std::abs(det)>1e-6){double t=((a.x-p[0])*dz-(a.z-p[2])*dx)/det;if(t>=0 && t<=1)candidates[count++]=t;}
   for(int k=0;k<count;++k){double t=candidates[k],x=p[0]+t*ex,z=p[2]+t*ez;
    double along=std::clamp(((x-a.x)*dx+(z-a.z)*dz)/nn,0.,1.);double rx=a.x+along*dx,rz=a.z+along*dz;
    double width=std::min(a.left+(b.left-a.left)*along,a.right+(b.right-a.right)*along)*.92;
    double y=a.y+(b.y-a.y)*along;
    if(width>32 && std::hypot(x-rx,z-rz)<width && high>=y-768 && low<=y+64 && (high-low>=64 || high<y-64))return true;
   }
  }
 }
 return false;
}

bool rage_oval_extend(const RageTrackCourse& active,RageTrackCourse& drawing){
 if(oval.faces.empty() || pixels.empty() || std::getenv("RAGE_DISABLE_OVAL"))return false;
 std::map<Quad,std::vector<Point>> shapes;for(const auto& f:active.faces){Point p;auto k=shape(active,f,p);shapes[k].push_back(p);}
 std::map<Point,unsigned> votes;for(const auto& f:oval.faces){Point p;auto k=shape(oval,f,p);auto it=shapes.find(k);if(it==shapes.end() || it->second.size()>8)continue;for(auto q:it->second){for(int j=0;j<3;++j)q[j]-=p[j];++votes[q];}}
 Point shift{};unsigned best=0;for(const auto& v:votes)if(v.second>best){best=v.second;shift=v.first;}if(best<500)return false;
 std::set<Key> exact;std::vector<Surface> existing;
 // Use authored triangles, before active topology splits, for stable surface
 // identity across T-junction repair. The racing course always wins overlaps.
 for(const auto& f:active.faces)for(const auto& t:corners){Key p;for(int k=0;k<3;++k)p[k]=point(active,f.vertex[t[k]]);exact.insert(key(p[0],p[1],p[2]));Surface s;if(surface(p,s))existing.push_back(s);}
 drawing=active;size_t added=0,shared=0,clipped=0,barriers=0;std::set<Key> emitted;
 for(const auto& f:oval.faces){
  Quad aligned;bool missing=false;for(int k=0;k<4;++k){aligned[k]=point(oval,f.vertex[k]);for(int j=0;j<3;++j)aligned[k][j]+=shift[j];}
  for(const auto& t:corners)missing|=!exact.count(key(aligned[t[0]],aligned[t[1]],aligned[t[2]]));
  if(missing && blocks_route(aligned,active.route)){++barriers;continue;}
  RageTrackSector sector{};sector.first=(uint32_t)drawing.faces.size();for(int j=0;j<3;++j){sector.lo[j]=1e30f;sector.hi[j]=-1e30f;}
  for(const auto& t:corners){Polygon poly;Key original;for(int k=0;k<3;++k){Point p=point(oval,f.vertex[t[k]]);for(int j=0;j<3;++j)p[j]+=shift[j];original[k]=p;poly.push_back({p,double(f.uv[t[k]]&255),double((f.uv[t[k]]>>8)&255)});}auto canonical=key(original[0],original[1],original[2]);if(exact.count(canonical)){++shared;continue;}Surface test;if(!surface(original,test))continue;
   std::vector<Polygon> pieces{poly};
   // Recheck each resulting piece: a large sloped face may only become
   // coplanar with a neighbouring receiver after an earlier clip.
   for(int pass=0;pass<2;++pass)for(const auto& s:existing){
    bool separate=false;for(int j=0;j<3;++j)separate|=test.hi[j]<s.lo[j]-.26 || s.hi[j]<test.lo[j]-.26;if(separate)continue;
    std::vector<Polygon> next;
    for(auto& piece:pieces){bool coplanar=true;
     for(const auto& v:piece){double distance=-s.d;for(int j=0;j<3;++j)distance+=s.n[j]*v.p[j];if(std::abs(distance)>.26){coplanar=false;break;}}
     bool disjoint=false;
     if(coplanar)for(int j=0;j<3;++j)if(j!=s.axis){double lo=piece[0].p[j],hi=lo,sl=s.p[0][j],sh=sl;for(const auto& v:piece){lo=std::min(lo,v.p[j]);hi=std::max(hi,v.p[j]);}for(int k=1;k<3;++k){sl=std::min(sl,s.p[k][j]);sh=std::max(sh,s.p[k][j]);}disjoint|=hi<=sl || sh<=lo;}
     if(!coplanar || disjoint){next.push_back(std::move(piece));continue;}
     auto result=subtract(piece,s);for(auto& r:result)next.push_back(std::move(r));
    }
    pieces=std::move(next);if(pieces.empty())break;
   }
   if(pieces.size()!=1 || pieces[0].size()!=3)++clipped;
   for(const auto& piece:pieces)for(size_t k=1;k+1<piece.size();++k){const V v[3]={piece[0],piece[k],piece[k+1]};Key rounded;for(int n=0;n<3;++n)for(int j=0;j<3;++j)rounded[n][j]=float(v[n].p[j]);Surface valid;if(!surface(rounded,valid) || std::abs(cross(rounded[0],rounded[1],rounded[2],valid.axis))<1.0)continue;auto ck=key(rounded[0],rounded[1],rounded[2]);if(!emitted.insert(ck).second)continue;
    RageTrackFace face=f;face.first_triangle=(uint32_t)drawing.triangles.size();face.triangle_count=1;face.decal=0;RageTrackTriangle tri{};
    for(int n=0;n<3;++n){tri.vertex[n]=face.vertex[n]=(uint32_t)drawing.vertices.size();std::array<float,3> p;for(int j=0;j<3;++j){p[j]=float(v[n].p[j]);sector.lo[j]=std::min(sector.lo[j],p[j]);sector.hi[j]=std::max(sector.hi[j],p[j]);}drawing.vertices.push_back(p);tri.uv[2*n]=float(v[n].u);tri.uv[2*n+1]=float(v[n].v);}face.vertex[3]=face.vertex[2];drawing.faces.push_back(face);drawing.triangles.push_back(tri);++added;
   }
  }
  sector.count=(uint32_t)drawing.faces.size()-sector.first;
  if(sector.count){
   bool merge=drawing.sectors.size()>active.sectors.size();
   if(merge){const auto& prev=drawing.sectors.back();merge=prev.count+sector.count<=64;
    for(int j:{0,2})merge&=std::max(prev.hi[j],sector.hi[j])-std::min(prev.lo[j],sector.lo[j])<=16384;}
   if(merge){auto& prev=drawing.sectors.back();prev.count+=sector.count;for(int j=0;j<3;++j){prev.lo[j]=std::min(prev.lo[j],sector.lo[j]);prev.hi[j]=std::max(prev.hi[j],sector.hi[j]);}}
   else drawing.sectors.push_back(sector);
  }
 }
 const unsigned seam_repairs=added?rage_track_stitch_geometry(drawing):0;
 // An Oval race already contains its own environment. Do not append it twice.
 if(shared>oval.faces.size()*1.8 || !added){drawing={};return false;}
 std::printf("Oval scenery: alignment %.0f,%.0f,%.0f (%u votes), %zu shared, %zu added, %zu clipped, %zu route barriers omitted, %u seam repairs\n",shift[0],shift[1],shift[2],best,shared,added,clipped,barriers,seam_repairs);return true;
}

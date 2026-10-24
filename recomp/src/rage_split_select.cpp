#include "rage_split_select.h"
#include "rage_split_state.h"
#include "rage_car_preview.h"
#include "rage_controllers.h"
#include "rage_menu_draw.h"
#include "rage_region.h"
#include "pgxp.h"
#include "iso_reader.h"
#include <vector>
#include <array>
#include <memory>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
extern const char* rage_bundle_disc_path();
namespace {
using namespace rage_preview;
std::array<Car,32> cars;
const int models[]={0,0,0,0,1,1,1,2,2,3,3,3,3,3,4,4,4,4,5,5,5,6,6,7,7,7,8,8,9,10,11,12};
const char* names[]={"ERRISO","ABEILLE","PEGASE","ESPERANZA","ACCERON","BAYONET","HIJACK","FATALITA","ISTANTE","GHEPARDO","VAINQURE","BULSHADE","SQUALDON"};
rage_split::Selection selection;
auto& picked=selection.picked;auto& ready=selection.ready;auto& active=selection.active;
int uploaded[2]={-1,-1};uint32_t last_tick=~0u;
std::unique_ptr<PS1::ISOReader> disc;PS1::ISOFileEntry archive{};std::array<uint8_t,2048> index{};
std::vector<uint8_t> asset(int number){
 if(!disc){auto d=std::make_unique<PS1::ISOReader>();if(!d->Open(rage_bundle_disc_path()) || !d->FindFile("RAGE.BIN",archive) || !d->ReadSector(archive.lba,index.data()))return {};disc=std::move(d);}
 auto u=[&](int o){return uint32_t(index[o])|(uint32_t(index[o+1])<<8)|(uint32_t(index[o+2])<<16)|(uint32_t(index[o+3])<<24);};
 uint32_t sector=u(number*8),size=u(number*8+4);if(size>1024*1024 || uint64_t(sector)*2048+size>archive.size)return {};
 std::vector<uint8_t> result(size);uint8_t block[2048];for(uint32_t offset=0;offset<size;offset+=2048){if(!disc->ReadSector(archive.lba+sector+offset/2048,block))return {};std::copy_n(block,std::min(2048u,size-offset),result.data()+offset);}return result;
}
void load(int variant){auto& car=cars[variant];if(car.loaded)return;car=rage_preview::decode(asset(10+variant*2));car.loaded=true;if(car.faces.empty())std::fprintf(stderr,"Rage Racer: car preview unavailable for variant %d\n",variant);}

void upload(int seat){auto& car=cars[picked[seat]];if(car.image.size()!=32768||uploaded[seat]==picked[seat])return;gpu_write_gp0(0xa0000000);gpu_write_gp0(704+64*seat);gpu_write_gp0(64|(256u<<16));for(size_t i=0;i<car.image.size();i+=4)gpu_write_gp0(w(car.image,i));uploaded[seat]=picked[seat];}
void draw_car(CPUState* c,int seat,uint32_t tick){
 load(picked[seat]);upload(seat);const auto& car=cars[picked[seat]];
 Vec low{1e9,1e9,1e9},high{-1e9,-1e9,-1e9};
 for(const auto& f:car.faces)for(auto p:f.p){low.x=std::min(low.x,p.x);low.y=std::min(low.y,p.y);low.z=std::min(low.z,p.z);high.x=std::max(high.x,p.x);high.y=std::max(high.y,p.y);high.z=std::max(high.z,p.z);}
 const Vec centre{(low.x+high.x)/2,(low.y+high.y)/2,(low.z+high.z)/2};double radius=1;
 for(const auto& f:car.faces)for(auto p:f.p)radius=std::max(radius,std::sqrt((p.x-centre.x)*(p.x-centre.x)+(p.y-centre.y)*(p.y-centre.y)+(p.z-centre.z)*(p.z-centre.z)));
 const double fit=160/radius;
 struct Draw {Face face;Vec p[4];double depth=0;};std::vector<Draw> faces;const double yaw=.7+tick*.006,cy=std::cos(yaw),sy=std::sin(yaw),tilt=.32;
 for(const auto& f:car.faces){Draw d;d.face=f;for(int k=0;k<4;++k){const Vec p{(f.p[k].x-centre.x)*fit,(f.p[k].y-centre.y)*fit,(f.p[k].z-centre.z)*fit};double x=p.x*cy+p.z*sy,z=-p.x*sy+p.z*cy,y=p.y*std::cos(tilt)-z*std::sin(tilt);z=p.y*std::sin(tilt)+z*std::cos(tilt);const double scale=320/(1000+z);d.p[k]={(seat?240.:80.)+x*scale,250+y*scale*2,z};d.depth+=z;}
  double area=(d.p[1].x-d.p[0].x)*(d.p[2].y-d.p[0].y)-(d.p[1].y-d.p[0].y)*(d.p[2].x-d.p[0].x);if(area<=0)continue;faces.push_back(d);
 }
 // OT prepends: submit near-to-far so the final walk paints far-to-near.
 std::stable_sort(faces.begin(),faces.end(),[](const Draw& a,const Draw& b){return a.depth<b.depth;});
 for(const auto& d:faces){const auto& f=d.face;auto xy=[&](int k){return rage_menu::xy(int(d.p[k].x),int(d.p[k].y));};
  const uint32_t packet=c->read_word(0x1f800000);
  if(f.textured){unsigned page=f.page,clut=f.clut;const int px=(page&15)*64,cx=(clut&63)*16;if(px==704)page+=seat;if(cx>=704&&cx<768)clut+=seat*4;
   uint32_t p[]={0x2d808080,xy(0),uint32_t(f.uv[0]|(f.uv[1]<<8)|(clut<<16)),xy(1),uint32_t(f.uv[2]|(f.uv[3]<<8)|(page<<16)),xy(2),uint32_t(f.uv[4]|(f.uv[5]<<8)),xy(3),uint32_t(f.uv[6]|(f.uv[7]<<8))};rage_menu::packet(c,p,9,0);
  }else{uint32_t p[]={0x28000000|f.colour,xy(0),xy(1),xy(2),xy(3)};rage_menu::packet(c,p,5,0);}
  if(c->read_word(0x1f800000)>packet)for(int k=0;k<4;++k){const uint32_t word=packet+(f.textured?8+8*k:8+4*k);pgxp_store_host_vertex(word,c->read_word(word),d.p[k].x,d.p[k].y,1000+d.p[k].z);}
 }
}
}
int rage_split_active(){return active;}
void rage_split_enter(){selection.enter(rage_host_pad_buttons(0),rage_host_pad_buttons(1));uploaded[0]=uploaded[1]=-1;last_tick=~0u;}
void rage_split_update(CPUState* c){uint32_t tick=c->read_word(rage_region_address(0x8009e694));if(last_tick==tick)return;last_tick=tick;
 for(int seat=0;seat<2;++seat)selection.sample(seat,rage_host_pad_buttons(seat));
}
void rage_split_draw(CPUState* c){
 uint32_t tick=c->read_word(rage_region_address(0x8009e694));draw_car(c,0,tick);draw_car(c,1,tick);rage_menu::environment(c,0);rage_menu::white_palette();
 rage_menu::text(c,36,40,"CONTROLLER 1");rage_menu::text(c,196,40,"CONTROLLER 2");
 for(int s=0;s<2;++s){int x=s*160;rage_menu::text(c,x+48,103,ready[s]?"READY":"PICK CAR",false);rage_menu::arrow(c,x+8,235,false,tick);rage_menu::arrow(c,x+140,235,true,tick);rage_menu::text(c,x+80-int(std::char_traits<char>::length(names[models[picked[s]]]))*4,350,names[models[picked[s]]]);char variant[24];std::snprintf(variant,sizeof variant,"VARIANT %02d",picked[s]+1);rage_menu::text(c,x+40,374,variant);rage_menu::quad(c,x+32,92,96,40,96,0,104,48,0x7e40,0x3c);}
 rage_menu::text(c,40,398,"LEFT RIGHT TO PICK A CAR");rage_menu::text(c,48,446,"CONFIRM TO SELECT CAR");
 const uint32_t black[]={0x28000000u,rage_menu::xy(0,0),rage_menu::xy(320,0),rage_menu::xy(0,480),rage_menu::xy(320,480)};
 rage_menu::packet(c,black,5);rage_menu::environment(c);
}

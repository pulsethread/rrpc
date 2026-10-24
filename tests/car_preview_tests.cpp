#include "rage_car_preview.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cmath>
int main(int argc,char** argv){assert(argc==2);unsigned count=0;
 for(const auto& entry:std::filesystem::directory_iterator(argv[1])){
  const auto name=entry.path().filename().string();if(name.rfind("CAR_",0)!=0 || entry.path().extension()!=".1ST")continue;
  std::ifstream file(entry.path(),std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
  auto car=rage_preview::decode(bytes);assert(car.faces.size()>80 && car.faces.size()<4000 && car.image.size()==32768);
  for(const auto& face:car.faces)for(auto p:face.p)assert(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::abs(p.x)<1000&&std::abs(p.y)<1000&&std::abs(p.z)<1000);
  for(int side=0;side<2;++side){double low[3]={1e9,1e9,1e9},high[3]={-1e9,-1e9,-1e9};unsigned wheels=0;
   for(const auto& f:car.faces)if(f.part==(side?4:2)){++wheels;for(auto p:f.p){const double v[]={p.x,p.y,p.z};for(int k=0;k<3;++k){low[k]=std::min(low[k],v[k]);high[k]=std::max(high[k],v[k]);}}}
   assert(wheels>8);for(int k=0;k<3;++k){double expected=4*int16_t(rage_preview::h(bytes,k*2));if(k==0&&side)expected=-expected;assert(std::abs((low[k]+high[k])/2-expected)<2);}
  }
  bytes.resize(60);assert(rage_preview::decode(bytes).faces.empty());++count;
 }
 assert(count==32);assert(rage_preview::decode({}).faces.empty());
}

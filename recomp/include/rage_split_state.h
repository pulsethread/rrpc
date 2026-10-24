#pragma once
#include <array>
namespace rage_split {
struct Selection {
 static constexpr int car_count=32;
 std::array<int,2> picked{0,0};
 std::array<bool,2> ready{false,false};
 std::array<unsigned,2> held{0,0};
 bool active=false;
 void enter(unsigned first,unsigned second){active=true;ready={false,false};held={first,second};}
 void sample(int seat,unsigned now){
  if(seat<0 || seat>1 || !active)return;
  const unsigned pressed=now&~held[seat];held[seat]=now;
  if(pressed&0x90){if(ready[seat])ready[seat]=false;else if(seat==0)active=false;}
  if(!active)return;
  if(!ready[seat]){if(pressed&0x8000)picked[seat]=(picked[seat]+car_count-1)%car_count;if(pressed&0x2000)picked[seat]=(picked[seat]+1)%car_count;if(pressed&0x860)ready[seat]=true;}
 }
};
}

#include "rage_split_state.h"
#include <cassert>
int main(){
 rage_split::Selection s;s.enter(0x40,0);s.sample(0,0x40);assert(!s.ready[0]);
 auto press=[&](int seat,unsigned mask){s.sample(seat,0);s.sample(seat,mask);};
 for(int i=1;i<=32;++i){press(1,0x2000);assert(s.picked[1]==i%32 && s.picked[0]==0);}
 press(0,0x8000);assert(s.picked[0]==31 && s.picked[1]==0);
 press(1,0x40);assert(s.ready[1]&&!s.ready[0]);press(1,0x2000);assert(s.picked[1]==0);
 press(0,0x40);assert(s.ready[0]&&s.ready[1]);press(1,0x10);assert(!s.ready[1]&&s.ready[0]&&s.active);
 press(0,0x10);assert(!s.ready[0]&&s.active);press(1,0x10);assert(s.active);
 press(0,0x10);assert(!s.active);
 s.enter(0,0);assert(!s.ready[0]&&!s.ready[1]);press(1,0x8000);assert(s.picked[1]==31 && s.picked[0]==31);
}

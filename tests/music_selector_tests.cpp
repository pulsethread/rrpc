#include "rage_music_selector.h"
#include "rage_region.h"
#include "rage_boot_notice.h"
#include <cassert>
int main(){
    for(unsigned t=0;t<10000;++t)assert(rage_music_title_offset(20,t)==0);
    assert(rage_music_title_offset(22,999)==0);
    assert(rage_music_title_offset(22,1000)==1);
    assert(rage_music_title_offset(22,1149)==1);
    assert(rage_music_title_offset(22,1150)==2);
    assert(rage_music_title_offset(22,2149)==2);
    assert(rage_music_title_offset(22,2150)==1);
    assert(rage_music_title_offset(22,2300)==0);
    assert(rage_music_title_offset(21,999)==0);
    assert(rage_music_title_offset(21,1000)==1);
    assert(rage_music_title_offset(21,2000)==0);
    for(unsigned t=0;t<100000;++t)assert(rage_music_title_offset(160,t)<=140);
    // A confirm on any early frame must still visit setup1/display15.
    for(unsigned confirm=0;confirm<16;++confirm){unsigned timer=0;bool setup=false,display=false;
        for(unsigned frame=0;frame<140 && timer<121;++frame){timer=rage_music_round_timer(timer,frame>=confirm);++timer;
            setup|=timer==1;display|=timer==15;}
        assert(timer>=121 && setup && display);
    }
    assert(rage_music_round_timer(120,false)==119);
    assert(rage_music_round_timer(5,true,false)==5);
    assert(rage_music_round_timer(15,true,false)==31);
    assert(rage_music_round_timer(90,true,false)==31);
    assert(rage_music_round_timer(32,true,true)==120);
    RageMusicConfirmation gate;
    assert(!gate.update(true));assert(!gate.update(true));
    assert(!gate.update(false));assert(gate.update(true));assert(gate.update(false));
    gate={};assert(!gate.update(false));assert(gate.update(true));
    assert(rage_skip_boot_notice(0,1,0));
    assert(rage_skip_boot_notice(0,1,109));
    assert(!rage_skip_boot_notice(0,1,110));
    assert(!rage_skip_boot_notice(0,34,0));
    assert(!rage_skip_boot_notice(0,5,0));
    assert(!rage_skip_boot_notice(1,1,0));
    assert(!rage_skip_boot_notice(2,1,0));
    for(unsigned region=0;region<3;++region){
        rage_active_region=region;
        const uint32_t mapping[]={0x801e4b60,0x801e42ec,0x801e4868};
        assert(rage_region_address(0x801e4b60)==mapping[region]);
    }
}

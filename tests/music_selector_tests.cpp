#include "rage_music_selector.h"
#include "rage_region.h"
#include <cassert>
int main(){
    for(unsigned t=0;t<10000;++t)assert(rage_music_title_offset(18,t)==0);
    assert(rage_music_title_offset(20,999)==0);
    assert(rage_music_title_offset(20,1000)==1);
    assert(rage_music_title_offset(20,1299)==1);
    assert(rage_music_title_offset(20,1300)==2);
    assert(rage_music_title_offset(20,2299)==2);
    assert(rage_music_title_offset(20,2300)==1);
    assert(rage_music_title_offset(20,2600)==0);
    assert(rage_music_title_offset(19,999)==0);
    assert(rage_music_title_offset(19,1000)==1);
    assert(rage_music_title_offset(19,2000)==0);
    for(unsigned t=0;t<100000;++t)assert(rage_music_title_offset(160,t)<=142);
    RageMusicConfirmation gate;
    assert(!gate.update(true));assert(!gate.update(true));
    assert(!gate.update(false));assert(gate.update(true));assert(gate.update(false));
    gate={};assert(!gate.update(false));assert(gate.update(true));
    for(unsigned region=0;region<3;++region){
        rage_active_region=region;
        const uint32_t mapping[]={0x801e4b60,0x801e42ec,0x801e4868};
        assert(rage_region_address(0x801e4b60)==mapping[region]);
    }
}

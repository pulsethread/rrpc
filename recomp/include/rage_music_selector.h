#pragma once
#include <cstddef>
#include <cstdint>

// Each endpoint rests for one second; intermediate characters last 300 ms.
inline size_t rage_music_title_offset(size_t length,uint64_t elapsed){
    if(length<=18)return 0;
    const size_t distance=length-18;
    const uint64_t travel=(distance-1)*300;
    const uint64_t phase=elapsed%(2000+2*travel);
    if(phase<1000)return 0;
    if(phase<1000+travel)return 1+size_t((phase-1000)/300);
    if(phase<2000+travel)return distance;
    return distance-1-size_t((phase-2000-travel)/300);
}

struct RageMusicConfirmation {
    bool armed=false,previous=false,confirmed=false;
    bool update(bool down){
        if(!down)armed=true;
        if(armed && down && !previous)confirmed=true;
        previous=down;
        return confirmed;
    }
};

#pragma once
#include <cstddef>
#include <cstdint>

// Each endpoint rests for one second; intermediate characters last 150 ms.
inline constexpr size_t rage_music_title_width=20;
inline size_t rage_music_title_offset(size_t length,uint64_t elapsed){
    if(length<=rage_music_title_width)return 0;
    const size_t distance=length-rage_music_title_width;
    const uint64_t travel=(distance-1)*150;
    const uint64_t phase=elapsed%(2000+2*travel);
    if(phase<1000)return 0;
    if(phase<1000+travel)return 1+size_t((phase-1000)/150);
    if(phase<2000+travel)return distance;
    return distance-1-size_t((phase-2000-travel)/150);
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

// Let the stock ROUND setup (frame1) and display enable (frame15) run
// before accelerating the timer. Confirmation remains latched meanwhile.
inline uint32_t rage_music_round_timer(uint32_t timer,bool confirmed,bool chime_played=true){
    if(!confirmed && timer>119)return 119;
    if(confirmed && timer>=15 && !chime_played)return 31;
    if(confirmed && timer>=15 && timer<120)return 120;
    return timer;
}

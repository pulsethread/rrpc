#pragma once
#include <cstdint>

// Only PAL's initial 110-frame notice is skipped. The following timer==110
// branch must still run to switch from the notice display to the Namco logo.
inline bool rage_skip_boot_notice(unsigned region,uint32_t scene,uint32_t timer){
    return region==0 && scene==1 && timer<110;
}

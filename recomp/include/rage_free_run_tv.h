#pragma once
#include "rage_free_run_tv_art.h"
#include "rage_region.h"
#include "cpu_state.h"
#include "gpu.h"

// Keep the retail screen's geometry, timing, projection and model animation.
// Its message is a native-size replacement in the original four-bit tile.
struct RageFreeRunTV {
    std::array<uint16_t,896> previous{};
    bool uploaded=false,scope=false,mode_restored=false;
    uint32_t ret=0,sp=0,timer=0,draws=0;
    uint16_t mode=0;
    static void pixels(const std::array<uint16_t,896>& image){
        gpu_write_gp0(0xa0000000);gpu_write_gp0(896u|(352u<<16));gpu_write_gp0(28u|(32u<<16));
        for(size_t i=0;i<image.size();i+=2)gpu_write_gp0(image[i]|(uint32_t(image[i+1])<<16));
    }
    void restore_mode(CPUState* c){
        if(scope && !mode_restored){c->write_half(rage_region_address(0x801e4dac),mode);mode_restored=true;}
    }
    void dispatch(CPUState* c,uint32_t target,bool free_run){
        if(!free_run && !scope && !uploaded)return;
        const bool active=free_run && c->read_word(rage_region_address(0x801e42e4))==12;
        if(scope && (!active || target==ret || c->gpr[29]>sp)){restore_mode(c);scope=false;}
        if(!active){if(uploaded){pixels(previous);uploaded=false;}return;}
        if(target==0x3d6f0){
            restore_mode(c);scope=true;mode_restored=false;ret=rage_region_code(c->gpr[31]);sp=c->gpr[29];timer=c->gpr[4];draws=0;
            const uint32_t address=rage_region_address(0x801e4dac);mode=c->read_half(address);
            // This flag is scoped to the screen routine's draw guard and is
            // restored before submitting either animated model (or on exit).
            c->write_half(address,1);
            if(!uploaded)if(const auto* vram=gpu_get_vram()){
                for(unsigned y=0;y<32;++y)for(unsigned x=0;x<28;++x)previous[y*28+x]=vram[(352+y)*1024+896+x];
                pixels(rage_free_run_tv_pixels);uploaded=true;
            }
        }else if(scope && target==0x296b4 && c->gpr[4]==0x1f800000){
            restore_mode(c);
            c->gpr[5]=draws++==0?24+(timer/4)%16:7;
        }
    }
};

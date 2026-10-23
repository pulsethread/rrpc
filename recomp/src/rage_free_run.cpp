#include "rage_free_run.h"
#include "rage_region.h"
#include "gpu.h"
#include "rage_free_run_art.h"
#include "rage_free_world.h"
#include "rage_multiplayer.h"
#include "rage_multiplayer_art.h"
#include <algorithm>

namespace {
int selection=0;
bool free_run=false;
bool multiplayer=false;
bool exit_vehicle_select=false;
uint32_t addr(uint32_t p){return rage_region_address(p);}
uint32_t word(CPUState* c,uint32_t p){return c->read_word(addr(p));}
void store(CPUState* c,uint32_t p,uint32_t v){c->write_word(addr(p),v);}
int original_selection(int s){return (s==3 || s==4)?2:s>4?s-2:s;}
bool ram(uint32_t p,unsigned bytes){p&=0x1fffffffu;return p>=0x10000 && p<=0x200000-bytes;}
uint32_t xy(int x,int y){return uint16_t(x)|(uint32_t(uint16_t(y))<<16);}
void prepare_labels(){
    const uint16_t* vram=gpu_get_vram();
    if(!vram)return;
    // Keep both labels to the right of the retail rows and below the TM
    // sprite (u224..235, v176..183). Press Start uses u112..223, v160..175;
    // the copyright strip uses the full width at v240..255.
    for(int state=0;state<2;++state){
        const auto& label=rage_free_run_labels[state];
        const int top=448+16*state;
        bool matches=true;
        for(int y=0;y<16 && matches;++y)for(int x=0;x<28;++x)
            if(vram[(top+y)*1024+604+x]!=label[y*28+x]){matches=false;break;}
        if(matches)continue;
        gpu_write_gp0(0xa0000000);gpu_write_gp0(604|(uint32_t(top)<<16));gpu_write_gp0(28|(16u<<16));
        for(size_t i=0;i<label.size();i+=2)gpu_write_gp0(label[i]|(uint32_t(label[i+1])<<16));
    }
    // One native indexed row serves both original grey/red CLUTs. The
    // copyright occupies v240..255; this row ends immediately above it.
    bool matches=true;
    for(int y=0;y<16 && matches;++y)for(int x=0;x<28;++x)
        if(vram[(480+y)*1024+604+x]!=rage_multiplayer_label[y*28+x]){matches=false;break;}
    if(!matches){gpu_write_gp0(0xa0000000);gpu_write_gp0(604|(480u<<16));gpu_write_gp0(28|(16u<<16));
        for(size_t i=0;i<rage_multiplayer_label.size();i+=2)gpu_write_gp0(rage_multiplayer_label[i]|(uint32_t(rage_multiplayer_label[i+1])<<16));}
}
// The game's POLY_FT4 layout. Publish through the same ordering table and
// scratch cursor as the original rows, retaining fades and presentation paths.
void rect(CPUState* c,uint32_t& cursor,uint32_t ot,int x,int y,int w,int h,
          int u,int v,int uw,int vh,int clut,int page){
    if(!ram(cursor,40) || !ram(ot,4))return;
    const uint32_t p[]={0x09000000u|(c->read_word(ot)&0xffffffu),0x2d808080u,
        xy(x,y),uint32_t(u|(v<<8)|(clut<<16)),xy(x+w,y),uint32_t((u+uw)|((v&255)<<8)|(page<<16)),
        xy(x,y+h),uint32_t(u|(((v+vh)&255)<<8)),xy(x+w,y+h),uint32_t((u+uw)|(((v+vh)&255)<<8))};
    for(int i=0;i<10;++i)c->write_word(cursor+4*i,p[i]);
    c->write_word(ot,(c->read_word(ot)&0xff000000u)|(cursor&0xffffffu));cursor+=40;
}
void draw_menu(CPUState* c){
    const int state=word(c,0x8009f098),slide=word(c,0x801e6f1c);
    if(state==1 && slide<=1){selection=0;free_run=false;multiplayer=false;exit_vehicle_select=false;}
    const bool extra=word(c,0x8019cac0)!=0;
    if(!extra && selection==1)selection=0;
    uint32_t cursor=c->read_word(0x1f800000),ot=word(c,0x8019c900)+0xcc;
    if(!ram(cursor,7*8*40) || !ram(ot,4))return;
    prepare_labels();
    int row=0;
    for(int i=0;i<7;++i){
        if(i==1 && !extra)continue;
        const int h=state==1?std::clamp(slide-row*8,0,16):16;
        const int y=100+(extra?20:24)*row++;
        const bool highlight=i==selection && state!=1 && !(word(c,0x801e429c)&2);
        const int clut=highlight?0x7e86:0x7e85;
        if(i!=3 && i!=4){
            rect(c,cursor,ot,104,y,112,h,0,160+16*original_selection(i),112,16,clut,0x39);
        }else if(i==3){
            rect(c,cursor,ot,104,y,112,h,112,highlight?208:192,112,16,clut,0x39);
        }else{
            rect(c,cursor,ot,104,y,112,h,112,224,112,16,clut,0x39);
        }
    }
    c->write_word(0x1f800000,cursor);
}
}

extern "C" int rage_free_run_dispatch(CPUState* c,uint32_t target){
    if(!c || !c->read_word || !c->write_word || !c->read_byte || !c->read_half || !c->write_half)return 0;
    target=rage_region_code(target);
    if(free_run && rage_free_world_dispatch(c,target))return 1;
    switch(target){
    case 0x1b5dc:{
        const uint16_t buttons=c->read_half(addr(0x801e436e));
        const bool extra=word(c,0x8019cac0)!=0;
        const int direction=(buttons&0x1000)?-1:(buttons&0x4000)?1:0;
        if(direction){
            do{selection=(selection+direction+7)%7;}while(selection==1 && !extra);
            // Let the original routine handle navigation sound, confirmation,
            // loading, and mode selection. Both solo entries use its TA path.
            const int wanted=original_selection(selection);
            int previous=(wanted-direction+5)%5;
            if(!extra && previous==1)previous=(previous-direction+5)%5;
            store(c,0x801e4184,previous);
        }else store(c,0x801e4184,original_selection(selection));
        if(buttons&0x860){free_run=selection==3;multiplayer=selection==4;}
        return 0;
    }
    case 0x1b2d4:draw_menu(c);break;
    case 0x5aca0:
        if((!free_run && !multiplayer) || word(c,0x8019c9f8)!=1)return 0;
        if(exit_vehicle_select){
            const uint32_t progress=word(c,0x801e4fac);
            const uint32_t course=word(c,0x801e428c);
            if(ram(progress,20)){
                c->write_word(progress,course&3);
                c->write_word(progress+4,word(c,0x801e40d4));
                c->write_word(progress+8,word(c,0x8009e6a4));
                c->write_word(progress+16,course>>2);
            }
            store(c,0x801e428c,course&3);
            store(c,0x801e42e4,2);
            exit_vehicle_select=false;
            break;
        }
        // Course assets and the saved course are ready. Enter the stock
        // vehicle-screen initializer before any course-selection frame draws.
        store(c,0x8019c9f8,3);
        store(c,0x8009b340,4);
        store(c,0x8009b344,1); // Stock outgoing course renderer stays at zero opacity.
        store(c,0x8009b378,uint32_t(-1));
        store(c,0x8009b374,word(c,0x801e40d4));
        store(c,0x8009b34c,0);store(c,0x8009b350,0);
        store(c,0x8009b358,250000);store(c,0x8009b35c,0);
        store(c,0x8009b30c,1); // Shared menu chrome and button hints open with the vehicle screen.
        return 0;
    case 0x5568c:
        if(multiplayer){
            rage_multiplayer_draw(c);
            const uint16_t buttons=c->read_half(addr(0x801e436e));
            if(buttons&0x90){exit_vehicle_select=true;store(c,0x8019c9f8,1);break;}
            // Placeholder buttons may highlight, but never start a race,
            // customise a car or browse/change the player's vehicle.
            c->write_half(addr(0x801e436e),buttons&uint16_t(~0xa860u));
            c->write_half(addr(0x801e436a),c->read_half(addr(0x801e436a))&uint16_t(~0xa000u));
            return 0;
        }
        if(free_run && word(c,0x8009b308)==5)exit_vehicle_select=true;
        return 0;
    case 0x5131c:
        if(!multiplayer)return 0;
        rage_multiplayer_draw(c);
        break;
    case 0x551bc:
        if(!multiplayer)return 0;
        // Preserve the stock screen fade state while removing gear labels.
        if(ram(c->gpr[4],4)){
            const int step=int32_t(c->gpr[5]);
            const int value=step?int(std::clamp(int64_t(int32_t(c->read_word(c->gpr[4])))+step,int64_t(0),int64_t(508))):0;
            c->write_word(c->gpr[4],value);c->gpr[2]=value;
        }
        break;
    case 0x4fce8: // Grade, make and car name.
    case 0x496f0: // Performance graph.
    case 0x4cf30: // Showroom light burst.
    case 0x4f3ec: // Number of cars.
    case 0x49418: // Previous/next car arrows.
    case 0x52158: // Engine specification.
        if(!multiplayer)return 0;
        break;
    case 0x48078:{
        if(multiplayer && word(c,0x801e42e4)==8){
            const uint32_t shape=c->gpr[5];
            // Rounded row plates are part of the shared screen chrome.
            // Captions and the CAR/S heading are separate scripted sprites.
            if(ram(shape,8) && c->read_half(shape)>80 && c->read_half(shape+2)>=20)return 0;
            break;
        }
        if(!free_run || word(c,0x801e42e4)!=8 || word(c,0x8019c9f8)!=4)return 0;
        const uint32_t shape=c->gpr[5];
        if(!ram(shape,8))return 0;
        const uint16_t width=c->read_half(shape),height=c->read_half(shape+2);
        const uint8_t u=c->read_byte(shape+4),v=c->read_byte(shape+5);
        if(height==24 && width==36 && u==204 && v==0){
            // Reuse the original "exit" sprite, with the stock button's
            // position, colour, animation and selection highlight.
            c->gpr[5]=addr(0x8007fc68);
            return 0;
        }
        if(height==24 && width==32 && u==18 && v==96){
            // This suffix also establishes the texture page for all three
            // captions. Keep its stock draw path, but hold its animation at
            // x=-50 (width32), entirely outside the menu's clipping area.
            c->gpr[4]=0;
        }
        return 0;
    }
    case 0x363d4:
        if(free_run){store(c,0x8009af9c,15000);store(c,0x801e8a8c,0);}
        return 0;
    case 0x3591c:
        if(!free_run)return 0;
        // No lap timing, finish trigger, wrong-way disqualification or record
        // writes. Keep progress bounded for arbitrarily many forward/back laps.
        if(const uint32_t car=c->gpr[4];ram(car,0x19c)){
            const int32_t length=word(c,0x801e40d8);
            if(length>0){
                int32_t progress=int32_t(c->read_word(car+0x68))%length;
                if(progress<0)progress+=length;
                c->write_word(car+0x68,uint32_t(progress));
            }
            c->write_half(car+0x168,1);
        }
        c->gpr[2]=0;
        break;
    case 0x1c974: // Actual pre-race ROUND screen (logo, course and best times).
    case 0x21d68: // Record-entry header.
    case 0x22068: // Pre-race best-time panel.
    case 0x333dc: // Either direction is valid in the open Free run network.
    case 0x352b8: // Sector timing / records.
    case 0x33090: // Lap-time column.
    case 0x32e9c: // Time/lap HUD labels; tachometer is drawn independently.
    case 0x357bc: // Split-time panel while paused.
        if(!free_run)return 0;
        break;
    default:return 0;
    }
    c->pc=c->gpr[31];return 1;
}
extern "C" int rage_free_run_active(){return free_run;}
extern "C" int rage_multiplayer_active(){return multiplayer;}

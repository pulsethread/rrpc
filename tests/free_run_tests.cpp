#include "rage_free_run.h"
#include "rage_region.h"
#include "rage_free_run_art.h"
#include "rage_multiplayer_art.h"
#include "rage_multiplayer.h"
#include "rage_free_run_tv.h"
#include <cassert>
#include <cstring>
#include <vector>
int rage_free_world_dispatch(CPUState*,uint32_t){return 0;}
int tunnel_state=0;float tunnel_position[3];
extern "C" int rage_track_in_tunnel(float x,float y,float z){tunnel_position[0]=x;tunnel_position[1]=y;tunnel_position[2]=z;return tunnel_state;}
int controller_draws=0,online_mode=0;
extern "C" void rage_multiplayer_set_online(int v){online_mode=v;}
extern "C" int rage_multiplayer_online(){return online_mode;}
extern "C" void rage_multiplayer_switch(int){online_mode=!online_mode;}
extern "C" void rage_multiplayer_draw(CPUState*){++controller_draws;}
namespace {
std::vector<uint16_t> vram(1024*512,0x5a5a);
int upload_stage=0,upload_x=0,upload_y=0,upload_w=0,upload_h=0,upload_pixel=0;
}
extern "C" const uint16_t* gpu_get_vram(){return vram.data();}
extern "C" void gpu_write_gp0(uint32_t value){
    if(upload_stage==0){assert(value==0xa0000000);upload_stage=1;return;}
    if(upload_stage==1){upload_x=value&0xffff;upload_y=value>>16;upload_stage=2;return;}
    if(upload_stage==2){upload_w=value&0xffff;upload_h=value>>16;upload_pixel=0;upload_stage=3;return;}
    for(int i=0;i<2;++i){
        const int x=upload_x+upload_pixel%upload_w,y=upload_y+upload_pixel/upload_w;
        assert((x>=604 && x<632 && y>=432 && y<496) || (x>=896 && x<924 && y>=352 && y<384));
        vram.at(y*1024+x)=uint16_t(value>>(16*i));++upload_pixel;
    }
    if(upload_pixel==upload_w*upload_h)upload_stage=0;
}
namespace {
std::vector<uint8_t> memory(0x200000),scratch(1024);
uint8_t& byte(uint32_t p){p&=0x1fffffff;return p>=0x1f800000?scratch.at(p-0x1f800000):memory.at(p);}
uint8_t rb(uint32_t p){return byte(p);}
uint16_t rh(uint32_t p){return rb(p)|(rb(p+1)<<8);}
uint32_t rw(uint32_t p){return rh(p)|(uint32_t(rh(p+2))<<16);}
void wb(uint32_t p,uint8_t v){byte(p)=v;}
void wh(uint32_t p,uint16_t v){wb(p,v);wb(p+1,v>>8);}
void ww(uint32_t p,uint32_t v){wh(p,v);wh(p+2,v>>16);}
void set(uint32_t p,uint32_t v){ww(rage_region_address(p),v);}
int get(uint32_t p){return rw(rage_region_address(p));}
CPUState cpu{};
int hook(uint32_t p){return rage_free_run_dispatch(&cpu,rage_region_address(p));}
void press(uint16_t buttons,bool extra=false){
    wh(rage_region_address(0x801e436e),buttons);
    assert(hook(0x8001b5dc)==0);
    // Original menu navigation remains in the compiled game.
    int s=get(0x801e4184);
    if(buttons&0x1000){--s;if(!extra && s==1)--s;}
    else if(buttons&0x4000){++s;if(!extra && s==1)++s;}
    set(0x801e4184,(s+5)%5);
}
void opening(bool extra){
    set(0x8019cac0,extra);set(0x8009f098,1);set(0x801e6f1c,1);
    set(0x8019c900,0x80100000);ww(0x1f800000,0x80110000);
    assert(hook(0x8001b2d4)==1);
    // The upload may touch only the reserved rectangle, leaving the retail
    // logo, TM, Press Start, menu rows, copyright strip and palettes intact.
    assert(upload_stage==0);
    for(int y=0;y<512;++y)for(int x=0;x<1024;++x){
        const bool free=x>=604 && x<632 && y>=448 && y<480;
        const bool multi=x>=604 && x<632 && ((y>=480 && y<496) || (y>=432 && y<448));
        assert(vram[y*1024+x]==(free?rage_free_run_labels[(y-448)/16][((y-448)%16)*28+x-604]:
            multi?rage_multiplayer_labels[y<448?1:0][(y-(y<448?432:480))*28+x-604]:0x5a5a));
    }
    set(0x8009f098,2);
}
}
int main(){
    cpu.read_word=rw;cpu.read_half=rh;cpu.read_byte=rb;
    cpu.write_word=ww;cpu.write_half=wh;cpu.write_byte=wb;cpu.gpr[31]=0x80012344;
    for(unsigned region=0;region<3;++region){
        rage_active_region=region;
        set(0x801e42e4,12);const auto player=rage_region_address(0x8009e6d4);
        ww(player,100);ww(player+4,0x80078194);ww(player+0x60,200);ww(player+8,300);
        for(int lighting=0;lighting<4;++lighting)for(int tunnel:{0,1}){
            tunnel_state=tunnel;cpu.gpr[4]=4500;cpu.gpr[5]=1;cpu.gpr[6]=lighting;cpu.gpr[7]=80;
            assert(hook(0x8003351c)==0 && cpu.gpr[6]==unsigned(tunnel?2:0) && cpu.gpr[7]==0);
            assert(cpu.gpr[4]==4500 && cpu.gpr[5]==1);
            assert(tunnel_position[0]==400 && tunnel_position[1]==800 && tunnel_position[2]==1200);
        }
        set(0x801e42e4,8);cpu.gpr[6]=3;cpu.gpr[7]=80;hook(0x8003351c);
        assert(cpu.gpr[6]==3 && cpu.gpr[7]==80);
        for(bool extra:{false,true}){
            opening(extra);
            const std::vector<int> order=extra?std::vector<int>{1,2,2,2,3,4,0}:std::vector<int>{2,2,2,3,4,0};
            for(int s:order){press(0x4000,extra);assert(get(0x801e4184)==s);}
            press(0x1000,extra);assert(get(0x801e4184)==4);
        }
        opening(false);
        press(0x4000);press(0x4000);press(0x40); // Free run, not Time attack.
        assert(get(0x801e4184)==2);
        cpu.gpr[4]=rage_region_address(0x8009e6d4);
        set(0x801e40d8,1000);
        ww(cpu.gpr[4]+0x68,1234567);wh(cpu.gpr[4]+0x168,32767);
        assert(hook(0x8003591c)==1 && cpu.gpr[2]==0 && cpu.pc==cpu.gpr[31]);
        assert(rw(cpu.gpr[4]+0x68)==567 && rh(cpu.gpr[4]+0x168)==1);
        ww(cpu.gpr[4]+0x68,uint32_t(-1234));
        assert(hook(0x8003591c)==1 && rw(cpu.gpr[4]+0x68)==766);
        for(uint32_t p:{0x800352b8u,0x80033090u,0x80032e9cu,0x800357bcu,0x800333dcu,0x80021d68u,0x80022068u})assert(hook(p)==1);
        set(0x801e42e4,10);set(0x801e42e0,128);
        assert(hook(0x8001c974)==0 && get(0x801e42e0)==128); // Logo stays on song selection.
        set(0x801e42e4,12);
        assert(hook(0x8001c974)==0 && get(0x801e42e0)==128); // Keep the original fade draw path.
        set(0x801e40b8,0);hook(0x800363d4);assert(get(0x801e40b8)==0 && get(0x801e42e0)==48);
        set(0x801e40b8,90);hook(0x800363d4);assert(get(0x801e40b8)==90);
        set(0x801e42e4,10);
        for(uint32_t offset:{0x80u}){cpu.gpr[31]=rage_region_address(0x8001c974)+offset;assert(hook(0x80046a2c)==0);}
        cpu.gpr[31]=rage_region_address(0x8001c974)+0xcc;assert(hook(0x80046a2c)==1);
        cpu.gpr[31]=rage_region_address(0x8001c974)+0x184;assert(hook(0x80046a2c)==1);
        cpu.gpr[31]=rage_region_address(0x8001c974)+0x11c;assert(hook(0x80016b7c)==1);
        cpu.gpr[31]=0x80012344;assert(hook(0x80046a2c)==0 && hook(0x80016b7c)==0);

        set(0x8009af9c,0);set(0x801e8a8c,120);assert(hook(0x800363d4)==0 && get(0x8009af9c)>0 && get(0x801e8a8c)==0);
        set(0x8019c9f8,0);assert(hook(0x8005aca0)==0 && get(0x8019c9f8)==0);
        set(0x801e428c,6);set(0x801e40d4,3);set(0x8009e6a4,1);
        set(0x8019c9f8,1);assert(hook(0x8005aca0)==0);
        assert(get(0x8019c9f8)==3 && get(0x8009b340)==4);
        assert(get(0x801e428c)==6 && get(0x8009b34c)==0 && get(0x8009b358)==250000);
        assert(get(0x8009b30c)==1);
        set(0x8019c9f8,4);set(0x801e42e4,8);set(0x8009b308,0);
        const uint32_t shape=0x80120000;
        cpu.gpr[5]=shape;wh(shape,36);wh(shape+2,24);wb(shape+4,204);wb(shape+5,0);
        assert(hook(0x80048078)==0 && cpu.gpr[5]==rage_region_address(0x8007fc68));
        cpu.gpr[5]=shape;wh(shape,32);wb(shape+4,18);wb(shape+5,96);
        cpu.gpr[4]=6;
        assert(hook(0x80048078)==0 && cpu.gpr[4]==0);
        set(0x8009b308,5);assert(hook(0x8005568c)==0);
        set(0x801e4fac,0x80180000);set(0x8019c9f8,1);
        assert(hook(0x8005aca0)==1 && get(0x801e42e4)==2);
        assert(rw(0x80180000)==2 && rw(0x80180004)==3 && rw(0x80180008)==1 && rw(0x80180010)==1);
        opening(false);press(0x4000);press(0x40); // Time attack keeps stock behavior.
        assert(hook(0x8003591c)==0 && hook(0x80033090)==0 && hook(0x800333dc)==0);
        assert(hook(0x80021d68)==0 && hook(0x80022068)==0 && hook(0x8001c974)==0);
        set(0x8019c9f8,1);assert(hook(0x8005aca0)==0 && get(0x8019c9f8)==1);
        assert(hook(0x80018000)==0);
        opening(false);press(0x4000);press(0x4000);press(0x4000);press(0x40);
        assert(rage_multiplayer_active() && !rage_free_run_active());
        assert(get(0x801e4184)==2);
        set(0x8019c9f8,1);assert(hook(0x8005aca0)==0 && get(0x8019c9f8)==1);
        set(0x8019c9f8,4);set(0x801e42e4,8);set(0x8009b308,0);
        wh(rage_region_address(0x801e436e),0xa860);wh(rage_region_address(0x801e436a),0xa000);
        assert(hook(0x8005568c)==0);
        assert(rh(rage_region_address(0x801e436e))==0 && rh(rage_region_address(0x801e436a))==0);
        wh(rage_region_address(0x801e436e),0);hook(0x8005568c);rage_multiplayer_set_online(0);
        wh(rage_region_address(0x801e436e),0x2000);assert(hook(0x8005568c)==0 && online_mode==1);
        wh(rage_region_address(0x801e436e),0);hook(0x8005568c);
        wh(rage_region_address(0x801e436e),0x8000);assert(hook(0x8005568c)==0 && online_mode==0);
        const int draws=controller_draws;assert(hook(0x8005131c)==1 && controller_draws==draws+1);
        for(uint32_t p:{0x8004fce8u,0x800496f0u,0x8004f3ecu,0x80049418u,0x80052158u,0x80048078u})assert(hook(p)==1);
        cpu.gpr[4]=0x80120000;ww(cpu.gpr[4],500);cpu.gpr[5]=20;
        assert(hook(0x800551bc)==1 && rw(cpu.gpr[4])==508 && cpu.gpr[2]==508);
        cpu.gpr[5]=0;assert(hook(0x800551bc)==1 && rw(cpu.gpr[4])==0);
        wh(rage_region_address(0x801e436e),0x10);
        assert(hook(0x8005568c)==1 && get(0x8019c9f8)==1);
        assert(hook(0x8005aca0)==1 && get(0x801e42e4)==2);
        opening(false);assert(!rage_multiplayer_active());
        for(uint32_t p:{0x8004fce8u,0x800496f0u,0x8004f3ecu,0x80049418u,0x80052158u,0x8005131cu})assert(hook(p)==0);
        // TV drawing borrows only the halfword draw guard, restores it before
        // model submission, selects the normal loop and preserves other modes.
        RageFreeRunTV tv;set(0x801e42e4,12);set(0x801e4dac,0x55aa0000);
        cpu.gpr[29]=0x801ff000;cpu.gpr[31]=0x80012344;cpu.gpr[4]=20;
        const auto oldPixel=vram[352*1024+896];tv.dispatch(&cpu,0x3d6f0,true);
        assert(get(0x801e4dac)==0x55aa0001);
        assert(vram[352*1024+896]==rage_free_run_tv_pixels[0]);
        cpu.gpr[4]=0x1f800000;cpu.gpr[5]=1;tv.dispatch(&cpu,0x296b4,true);
        assert(cpu.gpr[5]==29 && get(0x801e4dac)==0x55aa0000);
        tv.dispatch(&cpu,0x296b4,true);assert(cpu.gpr[5]==7);
        tv.dispatch(&cpu,0x12344,false);assert(vram[352*1024+896]==oldPixel);
        cpu.gpr[4]=4;tv.dispatch(&cpu,0x3d6f0,true);assert(get(0x801e4dac)==0x55aa0001);
        tv.dispatch(&cpu,0x12344,true);assert(get(0x801e4dac)==0x55aa0000);
        tv.dispatch(&cpu,0x18000,false);
    }
}

int rage_split_active(){return 0;}
void rage_split_enter(){}
void rage_split_update(CPUState*){}
void rage_split_draw(CPUState*){}

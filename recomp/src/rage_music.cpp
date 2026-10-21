#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "rage_music.h"
#include "rage_music_selector.h"
#include "song_catalog.h"
#include "rage_region.h"
#include "cpu_state.h"
#include "execution_profile.h"
#include "minimp3_ex.h"
#include <windows.h>
#include <memory>
#include <cstring>
#include <cstdio>
#include <algorithm>

extern "C" int psx_game_text_native_ok_full(uint32_t);
extern "C" {int rage_music_dispatch_pending=0;}
namespace {
std::vector<RageSong> songs;
unsigned selected=0;bool in_round=false;
uint64_t title_started=0;
RageMusicConfirmation confirmation;
uint32_t address(uint32_t p);
bool accelerator_down(CPUState* cpu){
    const uint32_t pad=address(0x801e436e)-6;
    const bool negcon=(cpu->read_half(pad)>>8)==0x23;
    if(negcon){
        const unsigned mapping=cpu->read_half(address(0x8019cb08));
        if(mapping==0 || mapping==5)return cpu->read_half(pad+12)>=0x36;
        if(mapping==1 || mapping==3 || mapping==6)return cpu->read_half(pad+14)>=0x36;
    }
    return (cpu->read_half(pad+2)&cpu->read_half(address(0x801e4b60)+(negcon?10:2)*2))!=0;
}
uint64_t custom_reads=0,custom_restarts=0;
uint32_t round_return=0,round_sp=0,draw_return=0,draw_sp=0;
uint32_t saved_selection=0;uint16_t saved_buttons=0;
void (*write_word)(uint32_t,uint32_t)=nullptr;
bool loop_saved=false;uint32_t old_loop=0;
struct Custom {mp3dec_ex_t decoder{};bool opened=false;uint32_t origin=0,sectors=0;uint64_t next=0;~Custom(){if(opened)mp3dec_ex_close(&decoder);}};
std::unique_ptr<Custom> custom;
uint32_t address(uint32_t p){return rage_region_address(p);}
void clear_custom(){custom.reset();if(loop_saved && write_word){write_word(address(0x8007f5b0)+17*4,old_loop);loop_saved=false;}}
void load_songs(){
    wchar_t path[32768];const DWORD n=GetModuleFileNameW(nullptr,path,32768);
    if(n && n<32768)songs=rage_song_catalog(std::filesystem::path(std::wstring(path,n)).parent_path());
    if(songs.empty())songs.push_back({"RANDOM PLAY",0,{}});
    selected=std::min<unsigned>(selected,unsigned(songs.size()-1));
}
uint32_t decode_location(uint32_t loc){
    auto bcd=[](uint32_t b){return (b>>4)*10+(b&15);};
    const uint32_t msf=(bcd(loc&255)*60+bcd((loc>>8)&255))*75+bcd((loc>>16)&255);
    return msf>=150?msf-150:0;
}
void commit(CPUState* cpu){
    clear_custom();if(!selected || selected>=songs.size())return;
    const auto& song=songs[selected];cpu->write_word(address(0x801e40e0),uint32_t(song.cd_track-3));
    if(song.file.empty())return;
    auto active=std::make_unique<Custom>();
    if(mp3dec_ex_open_w(&active->decoder,song.file.c_str(),MP3D_SEEK_TO_SAMPLE)!=0)return;
    active->opened=true;active->origin=decode_location(cpu->read_word(address(0x8009afd4)+17*4));
    if(active->decoder.info.hz!=44100 || active->decoder.info.channels!=2 || !active->decoder.samples ||
       active->decoder.samples>uint64_t(44100)*2*60*20 || !active->origin)return;
    active->sectors=uint32_t((active->decoder.samples+1175)/1176);
    if(active->origin+active->sectors>449999)return;
    // The original five-minute restart threshold must not truncate a longer
    // custom song. Hardware EOF still drives the normal track restart.
    old_loop=cpu->read_word(address(0x8007f5b0)+17*4);loop_saved=true;
    cpu->write_word(address(0x8007f5b0)+17*4,0x00005999);
    std::fprintf(stderr,"Music: custom selection %u, %u sectors\n",selected,active->sectors);
    custom=std::move(active);
}
void text_argument(CPUState* cpu,const std::string& text){
    // DrawBgmSelector owns an88-byte local text buffer at SP+40. Reuse that
    // buffer only during its verified text calls; no spare guest RAM arena.
    const uint32_t p=cpu->gpr[29]+40,physical=p&0x1fffffffu;
    if((p>>24)!=0x80 || physical>0x200000-88 || text.size()>=88)return;
    for(size_t i=0;i<=text.size();++i)cpu->write_byte(p+uint32_t(i),i<text.size()?uint8_t(text[i]):0);
    cpu->gpr[6]=p;
}
}
extern "C" void rage_music_dispatch(CPUState* cpu,uint32_t raw_target){
    const uint32_t target=raw_target<0x80000000u?raw_target:rage_region_code(raw_target);
    if(target!=0x1cfb4 && target!=0x1cd54 && target!=0x16754 && target!=round_return && target!=draw_return)return;
    if(!PSX_EXECUTION_ENHANCED || !cpu || !cpu->read_word || !cpu->write_word || !cpu->read_half || !cpu->write_half || !cpu->write_byte)return;
    const uint32_t sp=cpu->gpr[29],entry=address(0x80000000u|target);
    if(draw_return && target==draw_return && sp==draw_sp){
        cpu->write_word(address(0x801e42cc),saved_selection);draw_return=0;
    }
    if(round_return && target==round_return && sp==round_sp){
        cpu->write_half(address(0x801e436e),saved_buttons);round_return=0;
        if(cpu->read_word(address(0x801e42e4))!=10){commit(cpu);in_round=false;rage_music_dispatch_pending=0;}
    }
    if(target!=0x1cfb4 && target!=0x1cd54 && target!=0x16754)return;
    write_word=cpu->write_word;
    if(target==0x1cfb4 && cpu->read_word(address(0x801e42e4))==10 && psx_game_text_native_ok_full(entry)){
        rage_music_dispatch_pending=1;
        if(!in_round){if(songs.empty())selected=cpu->read_word(address(0x801e42cc));load_songs();in_round=true;clear_custom();title_started=GetTickCount64();confirmation={};}
        saved_buttons=cpu->read_half(address(0x801e436e));
        const unsigned previous_selection=selected;
        if(!confirmation.confirmed){
            if(saved_buttons&0x8000)selected=(selected+unsigned(songs.size())-1)%unsigned(songs.size());
            else if(saved_buttons&0x2000)selected=(selected+1)%unsigned(songs.size());
        }
        if(selected!=previous_selection)title_started=GetTickCount64();
        const bool confirmed=confirmation.update(accelerator_down(cpu));
        const uint32_t timer=cpu->read_word(address(0x801e40b8));
        // UpdateRoundScreen increments this timer before its original 121-frame
        // transition. Loading and fading continue normally while we wait.
        if(!confirmed && timer>119)cpu->write_word(address(0x801e40b8),119);
        else if(confirmed && timer<120)cpu->write_word(address(0x801e40b8),120);
        cpu->write_half(address(0x801e436e),saved_buttons&~0xa000);
        // Keep original shuffle/count/save storage within its original bounds.
        const uint32_t base=selected<songs.size() && songs[selected].file.empty() && songs[selected].cd_track>=3 && songs[selected].cd_track<=11?uint32_t(songs[selected].cd_track-2):0;
        const uint32_t count=cpu->read_word(address(0x801e40a8));
        cpu->write_word(address(0x801e42cc),base<=count?base:0);
        round_return=rage_region_code(cpu->gpr[31]);round_sp=sp;return;
    }
    if(target==0x1cd54 && in_round && !songs.empty() && cpu->read_word(entry)==0x27bdff60 && psx_game_text_native_ok_full(entry)){
        saved_selection=cpu->read_word(address(0x801e42cc));
        // Value10 selects the original two-digit arrow/number layout. Its
        // name-table index is valid, even for an independently numbered song.
        cpu->write_word(address(0x801e42cc),std::min(selected,10u));
        draw_return=rage_region_code(cpu->gpr[31]);draw_sp=sp;return;
    }
    if(target==0x16754 && draw_return && sp==draw_sp-160 && cpu->gpr[5]==0xce){
        if(cpu->gpr[4]==0x90)text_argument(cpu,songs[selected].name.substr(rage_music_title_offset(songs[selected].name.size(),GetTickCount64()-title_started),rage_music_title_width));
        else if(cpu->gpr[4]==0x74 || cpu->gpr[4]==0x78)text_argument(cpu,std::to_string(selected));
    }
}
extern "C" uint32_t rage_music_custom_origin(){return custom?custom->origin:0;}
extern "C" uint32_t rage_music_custom_sectors(){return custom?custom->sectors:0;}
extern "C" unsigned rage_music_selection(){return selected;}
extern "C" unsigned rage_music_title_position(){return in_round && selected<songs.size()?unsigned(rage_music_title_offset(songs[selected].name.size(),GetTickCount64()-title_started)):0;}
extern "C" int rage_music_waiting_confirmation(){return in_round && !confirmation.confirmed;}
extern "C" unsigned rage_music_song_count(){return unsigned(songs.size());}
extern "C" uint64_t rage_music_custom_reads(){return custom_reads;}
extern "C" uint64_t rage_music_custom_restarts(){return custom_restarts;}
extern "C" void rage_music_track_started(int track){if(track!=17)clear_custom();}
extern "C" int rage_music_custom_read(uint32_t sector,uint8_t* bytes){
    if(!custom || !bytes || sector>=custom->sectors)return 0;
    const uint64_t sample=uint64_t(sector)*1176;
    if(!sample && custom->next)++custom_restarts;
    if(custom->next!=sample && mp3dec_ex_seek(&custom->decoder,sample)!=0)return 0;
    const size_t want=size_t(std::min<uint64_t>(1176,custom->decoder.samples-sample));
    std::memset(bytes,0,2352);
    if(mp3dec_ex_read(&custom->decoder,reinterpret_cast<mp3d_sample_t*>(bytes),want)!=want)return 0;
    custom->next=sample+want;++custom_reads;return 1;
}

#include "song_catalog.h"
#include "rage_music.h"
#include "rage_region.h"
#include "cpu_state.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <array>
#include <memory>
#include <cstring>
#include <stdexcept>
extern "C" int psx_game_text_native_ok_full(uint32_t){return 1;}
extern "C" unsigned rage_music_selection();
extern "C" unsigned rage_music_song_count();
namespace fs=std::filesystem;
static std::array<uint8_t,2*1024*1024> memory;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
uint32_t rw(uint32_t p){uint32_t n;check((p&0x1fffffff)<memory.size()-3,"Read span");std::memcpy(&n,memory.data()+(p&0x1fffffff),4);return n;}
uint16_t rh(uint32_t p){uint16_t n;std::memcpy(&n,memory.data()+(p&0x1fffffff),2);return n;}
void ww(uint32_t p,uint32_t n){check((p&0x1fffffff)<memory.size()-3,"Write span");std::memcpy(memory.data()+(p&0x1fffffff),&n,4);}
void wh(uint32_t p,uint16_t n){std::memcpy(memory.data()+(p&0x1fffffff),&n,2);}
void wb(uint32_t p,uint8_t n){memory[p&0x1fffffff]=n;}
uint32_t a(uint32_t p){return rage_region_address(p);}
int main(int argc,char** argv){try{
    if(argc==3 && std::string(argv[1])=="--export"){
        auto songs=rage_song_catalog(fs::u8path(argv[2]));std::cout<<"Exported songs.ini with "<<songs.size()-1<<" songs\n";return 0;
    }
    check(argc==2,"Expected a valid 44.1kHz stereo MP3 fixture.");wchar_t module[32768];GetModuleFileNameW(nullptr,module,32768);auto folder=fs::path(module).parent_path();
    auto initial=rage_song_catalog(folder);check(initial.size()==11 && initial[1].cd_track==3 && initial[10].cd_track==17,"Base music map");
    fs::copy_file(fs::u8path(argv[1]),folder/"music/test.mp3",fs::copy_options::overwrite_existing);
    {std::ofstream ini(folder/"songs.ini",std::ios::app);ini<<"\n[song.2]\nname=RENAMED BASE\n\n[song.11]\nname=CUSTOM SONG\nfile=test.mp3\n[song.12]\nname=INVALID PATH\nfile=../escape.mp3\n[song.13]\nname=MISSING FILE\nfile=missing.mp3\n";}
    auto catalog=rage_song_catalog(folder);check(catalog.size()==12 && catalog[2].name=="RENAMED BASE" && catalog[2].cd_track==4,"Catalog extension/rename");
    auto cpu=std::make_unique<CPUState>();cpu->read_word=rw;cpu->read_half=rh;cpu->write_word=ww;cpu->write_half=wh;cpu->write_byte=wb;
    for(unsigned region=0;region<3;++region){
        rage_active_region=region;memory.fill(0);
        ww(a(0x801e42e4),10);ww(a(0x801e40a8),9);ww(a(0x801e40b8),100);ww(a(0x8001cd54),0x27bdff60);
        ww(a(0x8009afd4)+17*4,0x00004010);ww(a(0x8007f5b0)+17*4,5); //10:40:00 carrier location.
        for(unsigned i=0;i<10;++i)wb(a(0x801e40a8)-32+i,0xa0+i);
        cpu->gpr[29]=0x801ff000;cpu->gpr[31]=0x80015000;wh(a(0x801e436e),0x8000);
        rage_music_dispatch(cpu.get(),a(0x8001cfb4));
        check(rage_music_selection()==11 && rage_music_song_count()==12,"Custom selector wrapping");
        check(rw(a(0x801e42cc))==0 && rh(a(0x801e436e))==0 && rw(a(0x801e40b8))==64,"Safe original selection/input/delay");
        cpu->gpr[29]=0x801fee00;cpu->gpr[31]=0x80015010;rage_music_dispatch(cpu.get(),a(0x8001cd54));
        check(rw(a(0x801e42cc))==10,"Two-digit original layout");
        cpu->gpr[29]-=160;cpu->gpr[4]=0x90;cpu->gpr[5]=0xce;rage_music_dispatch(cpu.get(),a(0x80016754));
        check(std::string(reinterpret_cast<char*>(memory.data()+(cpu->gpr[6]&0x1fffffff)))=="\"CUSTOM SONG\"","Custom title");
        cpu->gpr[4]=0x74;rage_music_dispatch(cpu.get(),a(0x80016754));
        check(std::string(reinterpret_cast<char*>(memory.data()+(cpu->gpr[6]&0x1fffffff)))=="11","Custom number");
        cpu->gpr[29]=0x801fee00;rage_music_dispatch(cpu.get(),0x80015010);check(rw(a(0x801e42cc))==0,"Original selection restored");
        ww(a(0x801e42e4),11);cpu->gpr[29]=0x801ff000;rage_music_dispatch(cpu.get(),0x80015000);
        check(rw(a(0x801e40e0))==14 && rage_music_custom_sectors()>0 && rage_music_custom_origin()>0,"Custom commit");
        std::array<uint8_t,2352> pcm{};check(rage_music_custom_read(0,pcm.data()) && rage_music_custom_read(rage_music_custom_sectors()-1,pcm.data()) && rage_music_custom_read(0,pcm.data()),"Custom seek/end/restart");
        rage_music_track_started(2);check(rage_music_custom_sectors()==0 && rw(a(0x8007f5b0)+17*4)==5,"Custom cleanup/loop restore");
        for(unsigned i=0;i<10;++i)check(memory[(a(0x801e40a8)-32+i)&0x1fffffff]==0xa0+i,"Original shuffle storage changed");
        check(rw(a(0x801e40a8))==9,"Original count expanded");
        // Return the independent cursor to RANDOM before the next region.
        ww(a(0x801e42e4),10);wh(a(0x801e436e),0x2000);cpu->gpr[31]=0x80015000;
        rage_music_dispatch(cpu.get(),a(0x8001cfb4));check(rage_music_selection()==0,"Random wrap");
        ww(a(0x801e42e4),11);rage_music_dispatch(cpu.get(),0x80015000);
    }
    std::cout<<"Catalog, custom navigation/text/playback, original-array bounds and all three region mappings passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

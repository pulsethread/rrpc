#include "cdda_archive.h"
#include <fstream>
#include <string>
#include <cstring>
#include <algorithm>

bool CDDAArchive::load(const std::filesystem::path& table,uint32_t data_sectors){
    std::ifstream in(table);std::string magic;uint32_t count=0;
    if(!(in>>magic>>leadout>>count) || magic!="RAGE_MP3_TOC_1" || count<1 || count>98 || leadout>450000)return false;
    uint32_t end=data_sectors;
    for(uint32_t i=0;i<count;++i){
        auto t=std::make_unique<Track>();std::string name;
        if(!(in>>t->number>>t->start>>t->pregap>>t->count>>name) || t->number!=i+2 ||
           t->pregap!=end || !t->count || t->start<t->pregap || t->start-t->pregap>=t->count ||
           t->count>leadout || t->pregap>leadout-t->count ||
           name!="audio"+std::to_string(t->number)+".mp3")return false;
        t->path=table.parent_path()/name;
        if(mp3dec_ex_open_w(&t->decoder,t->path.c_str(),MP3D_SEEK_TO_SAMPLE)!=0)return false;
        t->opened=true;
        if(t->decoder.info.channels!=2 || t->decoder.info.hz!=44100 ||
           t->decoder.samples!=uint64_t(t->count)*1176)return false;
        end=t->pregap+t->count;tracks.push_back(std::move(t));
    }
    std::string extra;return end==leadout && !(in>>extra);
}
bool CDDAArchive::contains(uint32_t lba)const{
    return !tracks.empty() && lba>=tracks.front()->pregap && lba<leadout;
}
bool CDDAArchive::read(uint32_t lba,uint8_t* bytes){
    for(auto& t:tracks)if(lba>=t->pregap && lba-t->pregap<t->count){
        const uint64_t frame=uint64_t(lba-t->pregap)*588;
        if(t->next_frame!=frame && mp3dec_ex_seek(&t->decoder,frame*2)!=0)return false;
        if(mp3dec_ex_read(&t->decoder,reinterpret_cast<mp3d_sample_t*>(bytes),1176)!=1176)return false;
        t->next_frame=frame+588;return true;
    }
    return false;
}

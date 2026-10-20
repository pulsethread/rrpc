#define NOMINMAX
#include "encode_cdda.h"
#include "iso_reader.h"
#include "lame.h"
#include "minimp3_ex.h"
#include <fstream>
#include <array>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <algorithm>

namespace fs=std::filesystem;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void cancelled(const std::atomic<bool>& value){check(!value.load(),"Cancelled. Your existing data.dat has not been changed.");}
}
std::vector<fs::path> encode_cdda(const fs::path& cue,const GenerateProgress& progress,const std::atomic<bool>& cancel){
    PS1::ISOReader disc;check(disc.Open(cue.string()),"Cannot mount staged audio tracks.");
    const int tracks=disc.TrackCount();if(tracks<=1)return {};
    check(!disc.TrackIsAudio(1) && disc.TrackStartLBA(1)==0,"Unsupported audio disc layout.");
    const uint32_t end=disc.GetSectorCount(),data_end=disc.TrackPregapLBA(2);
    std::vector<fs::path> generated;std::ostringstream toc;
    toc<<"RAGE_MP3_TOC_1 "<<end<<' '<<tracks-1<<'\n';
    for(int t=2;t<=tracks;++t){
        cancelled(cancel);check(disc.TrackIsAudio(t),"A data track was found after the music tracks.");
        const uint32_t first=disc.TrackPregapLBA(t),stop=t<tracks?disc.TrackPregapLBA(t+1):end;
        check(stop>first && stop<=450000,"Invalid music track length.");
        const auto path=cue.parent_path()/("audio"+std::to_string(t)+".mp3");
        std::unique_ptr<lame_global_flags,decltype(&lame_close)> encoder(lame_init(),lame_close);
        check(bool(encoder),"Cannot initialise the MP3 encoder.");auto* g=encoder.get();
        check(lame_set_num_channels(g,2)==0 && lame_set_in_samplerate(g,44100)==0 &&
            lame_set_out_samplerate(g,44100)==0 && lame_set_brate(g,320)==0 &&
            lame_set_VBR(g,vbr_off)==0 && lame_set_quality(g,0)==0 &&
            lame_set_bWriteVbrTag(g,1)==0 && lame_set_num_samples(g,(stop-first)*588u)==0 &&
            lame_init_params(g)==0,"Cannot configure high-quality MP3 encoding.");
        std::ofstream out(path,std::ios::binary);std::array<short,588*2*16> pcm{};std::array<unsigned char,65536> mp3{};
        for(uint32_t sector=first;sector<stop;){
            cancelled(cancel);const uint32_t n=std::min(16u,stop-sector);
            for(uint32_t i=0;i<n;++i)check(disc.ReadRawSector(sector+i,reinterpret_cast<uint8_t*>(pcm.data()+i*1176)),"Cannot read a music sector.");
            int bytes=lame_encode_buffer_interleaved(g,pcm.data(),n*588,mp3.data(),int(mp3.size()));
            check(bytes>=0,"MP3 encoding failed.");out.write(reinterpret_cast<char*>(mp3.data()),bytes);
            check(bool(out),"Cannot write MP3 audio.");sector+=n;
            progress(46,L"Encoding music to MP3: track "+std::to_wstring(t-1)+L" of "+std::to_wstring(tracks-1)+L" ("+std::to_wstring((sector-first)*100/(stop-first))+L"%)");
        }
        int bytes=lame_encode_flush(g,mp3.data(),int(mp3.size()));check(bytes>=0,"Cannot finish MP3 encoding.");
        out.write(reinterpret_cast<char*>(mp3.data()),bytes);
        const size_t tag=lame_get_lametag_frame(g,mp3.data(),mp3.size());
        check(tag>0 && tag<=mp3.size(),"Cannot write gapless MP3 timing.");
        out.seekp(0);out.write(reinterpret_cast<char*>(mp3.data()),tag);out.close();check(bool(out),"Cannot finish writing MP3 audio.");
        // Verify exact decoded duration, including delay/padding removal.
        mp3dec_ex_t verify{};check(mp3dec_ex_open_w(&verify,path.c_str(),MP3D_SEEK_TO_SAMPLE)==0,"Encoded MP3 could not be opened.");
        const bool valid=verify.info.channels==2 && verify.info.hz==44100 && verify.samples==uint64_t(stop-first)*1176;
        mp3dec_ex_close(&verify);check(valid,"MP3 timing verification failed.");
        toc<<t<<' '<<disc.TrackStartLBA(t)<<' '<<first<<' '<<stop-first<<' '<<path.filename().string()<<'\n';
        generated.push_back(path);
    }
    // Preserve every data-sector byte; audio is addressed by the original TOC.
    const auto data=cue.parent_path()/"data.bin";
    {std::ofstream out(data,std::ios::binary);std::array<uint8_t,2352> raw{};
        for(uint32_t i=0;i<data_end;++i){cancelled(cancel);check(disc.ReadRawSector(i,raw.data()),"Cannot read original data sectors.");out.write(reinterpret_cast<char*>(raw.data()),raw.size());}
        out.close();check(bool(out),"Cannot finish the data-track copy.");}
    disc.Close();
    {std::ofstream out(cue,std::ios::binary);out<<"FILE \"data.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n";out.close();check(bool(out),"Cannot write the music disc descriptor.");}
    const auto table=fs::path(cue).concat(".mp3toc");
    {std::ofstream out(table);out<<toc.str();out.close();check(bool(out),"Cannot write the music timing table.");}
    generated.push_back(data);generated.push_back(table);return generated;
}

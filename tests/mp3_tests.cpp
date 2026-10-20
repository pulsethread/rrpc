#include "encode_cdda.h"
#include "iso_reader.h"
#include <fstream>
#include <iostream>
#include <cmath>
#include <cstring>
#include <array>
#include <vector>
#include <stdexcept>

namespace fs=std::filesystem;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int compare_discs(const char* source,const char* packed){
    PS1::ISOReader raw,mp3;check(raw.Open(source) && mp3.Open(packed),"Cannot mount real-disc comparison.");
    check(raw.TrackCount()==mp3.TrackCount() && raw.GetSectorCount()==mp3.GetSectorCount(),"Real-disc TOC differs.");
    std::array<uint8_t,2352> a{},b{};uint64_t data_bytes=0,audio_frames=0;
    for(int t=1;t<=raw.TrackCount();++t){
        const uint32_t start=raw.TrackPregapLBA(t),end=t<raw.TrackCount()?raw.TrackPregapLBA(t+1):raw.GetSectorCount();
        check(start==mp3.TrackPregapLBA(t) && raw.TrackStartLBA(t)==mp3.TrackStartLBA(t),"Real-disc pregap/start differs.");
        double error=0,signal=0;std::vector<std::pair<uint32_t,std::array<uint8_t,2352>>> saved;
        for(uint32_t s=start;s<end;++s){check(raw.ReadRawSector(s,a.data()) && mp3.ReadRawSector(s,b.data()),"Real-disc sector decode failed.");
            if(t==1){check(a==b,"Real-disc data sector changed.");data_bytes+=2352;continue;}
            audio_frames+=588;
            if(s==start || s==start+(end-start)/4 || s==end-1)saved.push_back({s,b});
            for(int i=0;i<1176;++i){short x,y;std::memcpy(&x,a.data()+i*2,2);std::memcpy(&y,b.data()+i*2,2);error+=double(x-y)*(x-y);signal+=double(x)*x;}
        }
        for(auto it=saved.rbegin();it!=saved.rend();++it){std::cout<<"seek track "<<t<<" sector "<<it->first<<std::endl;check(mp3.ReadRawSector(it->first,b.data()),"Real-disc random seek failed.");check(b==it->second,"Real-disc seek differs from continuous playback.");}
        if(t>1){std::cout<<"track "<<t<<" frames "<<uint64_t(end-start)*588<<" relative error "<<(signal?error/signal:0)<<std::endl;check(!signal || error/signal<.08,"Real-disc audio is shifted or damaged.");}
    }
    std::cout<<"Verified unchanged data bytes "<<data_bytes<<", decoded stereo frames "<<audio_frames<<std::endl;return 0;
}
int main(int argc,char** argv){try{
    if(argc==4 && std::string(argv[1])=="--compare")return compare_discs(argv[2],argv[3]);
    check(argc==2,"Expected isolated output directory.");const fs::path dir=fs::absolute(argv[1]);fs::create_directories(dir);
    const auto cue=dir/"fixture.cue";
    fs::remove(fs::path(cue).concat(".mp3toc"));
    {std::ofstream out(dir/"raw.bin",std::ios::binary);
        for(int s=0;s<450;++s){std::array<short,1176> pcm{};
            for(int f=0;f<588;++f){int n=s*588+f;pcm[f*2]=short(9000*std::sin(n*.063));pcm[f*2+1]=short(7000*std::cos(n*.041));}
            out.write(reinterpret_cast<char*>(pcm.data()),2352);}}
    {std::ofstream out(cue);out<<"FILE \"raw.bin\" BINARY\n TRACK 01 MODE2/2352\n INDEX 01 00:00:00\n TRACK 02 AUDIO\n INDEX 00 00:00:30\n INDEX 01 00:00:40\n TRACK 03 AUDIO\n INDEX 00 00:03:00\n INDEX 01 00:03:10\n";}
    PS1::ISOReader raw;check(raw.Open(cue.string()),"Raw fixture mount failed.");
    std::atomic<bool> cancel{false};encode_cdda(cue,[](int,const std::wstring&){},cancel);
    PS1::ISOReader mp3;check(mp3.Open(cue.string()),"MP3 mount failed.");
    check(raw.TrackCount()==mp3.TrackCount() && raw.GetSectorCount()==mp3.GetSectorCount(),"TOC count/end changed.");
    for(int t=1;t<=3;++t)check(raw.TrackStartLBA(t)==mp3.TrackStartLBA(t) && raw.TrackPregapLBA(t)==mp3.TrackPregapLBA(t),"Track/pregap shifted.");
    std::array<uint8_t,2352> a{},b{},again{};
    for(int s=0;s<30;++s){check(raw.ReadRawSector(s,a.data()) && mp3.ReadRawSector(s,b.data()),"Data read failed.");check(a==b,"Data changed.");}
    double error=0,signal=0;std::vector<std::array<uint8_t,2352>> sequential(450);
    for(int s=30;s<450;++s){check(raw.ReadRawSector(s,a.data()) && mp3.ReadRawSector(s,b.data()),"Audio sector missing.");
        sequential[s]=b;
        for(int i=0;i<1176;++i){short x,y;std::memcpy(&x,a.data()+i*2,2);std::memcpy(&y,b.data()+i*2,2);error+=double(x-y)*(x-y);signal+=double(x)*x;}}
    check(error/signal<.025,"Decoded music shifted or heavily distorted.");
    for(int s:{449,30,250,224,225,40,300,30}){check(mp3.ReadRawSector(s,a.data()),"Random seek failed.");
        if(a!=sequential[s]){
            int max_difference=0;double difference=0;
            for(int i=0;i<1176;++i){short x,y;std::memcpy(&x,a.data()+i*2,2);std::memcpy(&y,sequential[s].data()+i*2,2);int delta=std::abs(int(x)-y);max_difference=std::max(max_difference,delta);difference+=delta;}
            std::cout<<"seek "<<s<<" max difference "<<max_difference<<" mean "<<difference/1176<<'\n';
            check(max_difference<=1,"Random seek differs from continuous playback.");
        }
        check(mp3.ReadRawSector(s,b.data()),"Repeat seek failed.");check(a==b,"Seek was not sample exact.");}
    check(!mp3.ReadRawSector(450,b.data()),"Read past leadout accepted.");
    {std::ofstream out(fs::path(cue).concat(".mp3toc"));out<<"RAGE_MP3_TOC_1 450 1\n2 40 30 420 ../escape.mp3\n";}
    PS1::ISOReader bad;check(!bad.Open(cue.string()),"Invalid timing table accepted.");
    std::cout<<"MP3 roundtrip, original TOC/pregaps, unchanged data, exact seeks and invalid-table rejection passed; relative error "<<error/signal<<"\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

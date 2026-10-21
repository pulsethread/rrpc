#include "song_catalog.h"
#include "minimp3_ex.h"
#include <fstream>
#include <map>
#include <algorithm>
#include <cstdio>
#include <cctype>

namespace {
const char* names[]={"RAGE RACER","MATHEMABEAT","LIGHTNING LUGE","INDUSTRIA","HURRICANE HUB","MECH MONSTER","SILVER STREAM","STIMULATION","VOLCANO VEHICLE","DEEP DRIVE"};
std::string trim(std::string s){const auto a=s.find_first_not_of(" \t\r\n");if(a==std::string::npos)return {};return s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);}
bool safe_name(const std::string& s){return !s.empty() && s.size()<=160 && std::all_of(s.begin(),s.end(),[](unsigned char c){return c>=32 && c<127;});}
}
std::vector<RageSong> rage_song_catalog(const std::filesystem::path& folder){
    namespace fs=std::filesystem;
    const auto ini=folder/"songs.ini";fs::create_directories(folder/"music");
    if(!fs::exists(ini)){
        std::ofstream out(ini);
        out<<"; Original selection 0 = RANDOM PLAY. Songs 1..9 use CD tracks 3..11; song 10 uses CD17.\n"
              "; Add [song.11], name=Your title, file=your-song.mp3 (inside music/).\n"
              "; MP3: 44.1kHz stereo, up to 20 minutes. Entries are shown in numeric section order.\n"
              "; Names use the original ASCII font; long titles scroll in the selector.\n"
              "; Base cd_track mappings are fixed. A file= entry can also replace a base song.\n\n";
        for(int i=0;i<10;++i)out<<"[song."<<i+1<<"]\nname="<<names[i]<<"\ncd_track="<<(i==9?17:i+3)<<"\n\n";
    }
    std::map<int,std::map<std::string,std::string>> sections;std::ifstream in(ini);std::string line;int id=0;
    while(std::getline(in,line)){
        if(line.size()>4096)continue;
        if(line.compare(0,3,"\xef\xbb\xbf")==0)line.erase(0,3);
        line=trim(line);if(line.empty() || line[0]==';' || line[0]=='#')continue;
        if(line[0]=='['){id=0;int value=0;char tail=0;if(std::sscanf(line.c_str(),"[song.%d]%c",&value,&tail)==1 && value>=1 && value<=99)id=value;continue;}
        const auto equals=line.find('=');if(id && equals!=std::string::npos){
            auto value=trim(line.substr(equals+1));if(value.size()>=2 && value.front()=='"' && value.back()=='"')value=value.substr(1,value.size()-2);
            sections[id][trim(line.substr(0,equals))]=value;
        }
    }
    std::vector<RageSong> result{{"RANDOM PLAY",0,{}}};
    for(int i=1;i<=99;++i){
        auto found=sections.find(i);if(i>10 && found==sections.end())continue;
        RageSong song{i<=10?names[i-1]:"",i<=9?i+2:17,{}};
        if(found!=sections.end()){
            const auto& fields=found->second;auto name=fields.find("name");if(name!=fields.end() && safe_name(name->second))song.name=name->second;
            auto file=fields.find("file");
            if(file!=fields.end() && !file->second.empty()){
                const fs::path relative=fs::u8path(file->second);bool safe=!relative.is_absolute() && !relative.has_root_name();
                for(const auto& part:relative)if(part==".." || part==".")safe=false;
                auto extension=relative.extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
                const auto path=folder/"music"/relative;mp3dec_ex_t mp3{};
                if(!safe || extension!=".mp3" || mp3dec_ex_open_w(&mp3,path.c_str(),MP3D_SEEK_TO_SAMPLE)!=0){std::fprintf(stderr,"Music: cannot open song.%d file\n",i);continue;}
                const bool valid=mp3.info.channels==2 && mp3.info.hz==44100 && mp3.samples>0 && mp3.samples<=uint64_t(44100)*2*60*20;
                mp3dec_ex_close(&mp3);if(!valid){std::fprintf(stderr,"Music: song.%d needs 44.1kHz stereo MP3, at most20minutes\n",i);continue;}
                song.file=path;song.cd_track=17;
            }else if(i>10)continue;
        }
        if(!safe_name(song.name))continue;
        result.push_back(std::move(song));
    }
    return result;
}

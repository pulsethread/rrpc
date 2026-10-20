#define NOMINMAX
#include "generate_data.h"
#include "encode_cdda.h"
#include "support_files.h"
#include "region_profiles.h"
#include <windows.h>
#include <winioctl.h>
#include <ntddcdrm.h>
#include <bcrypt.h>
#include <fstream>
#include <sstream>
#include <regex>
#include <vector>
#include <array>
#include <algorithm>
#include <set>
#include <memory>
#include <stdexcept>
#include <cstdio>
#include <cstring>
namespace fs=std::filesystem;
namespace {
using Bytes=std::vector<char>;
void fail(const char* s){throw std::runtime_error(s);}
void check_cancel(const std::atomic<bool>& c){if(c.load())fail("Cancelled. Your existing data.dat has not been changed.");}
struct Hash {
 BCRYPT_ALG_HANDLE alg{};BCRYPT_HASH_HANDLE h{};
 Hash(){if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)||BCryptCreateHash(alg,&h,nullptr,0,nullptr,0,0))fail("Cannot initialise checksums.");}
 ~Hash(){if(h)BCryptDestroyHash(h);if(alg)BCryptCloseAlgorithmProvider(alg,0);}
 void add(const char* p,size_t n){if(BCryptHashData(h,(PUCHAR)p,ULONG(n),0))fail("Checksum failed.");}
 std::string finish(){std::string s(32,0);if(BCryptFinishHash(h,(PUCHAR)s.data(),32,0))fail("Checksum failed.");return s;}
};
std::string hex(const std::string& a){std::string s;for(unsigned char c:a){s+="0123456789abcdef"[c>>4];s+="0123456789abcdef"[c&15];}return s;}
void read(std::istream& f,char* b,size_t n){if(!f.read(b,n))fail("The source is incomplete or could not be read.");}
void write(std::ostream& f,const char* b,size_t n){if(!f.write(b,n))fail("Cannot write data. Check free space and folder permissions.");}
uint32_t le32(const char* p){return uint8_t(p[0])|(uint32_t(uint8_t(p[1]))<<8)|(uint32_t(uint8_t(p[2]))<<16)|(uint32_t(uint8_t(p[3]))<<24);}
void integer(std::ostream& o,uint64_t v,int n){for(int i=0;i<n;++i){char c=char(v>>(8*i));write(o,&c,1);}}
void text(const fs::path& path,const std::string& s){std::ofstream o(path,std::ios::binary);write(o,s.data(),s.size());}
struct Entry {std::string name;fs::path file;Bytes embedded;uint64_t size=0,offset=0;std::string digest;};
struct Work {fs::path path;bool owned=false;~Work(){if(owned){std::error_code ec;fs::remove_all(path,ec);}}};
struct Disc {fs::path file;uint32_t sector=2352,offset=24;uint64_t first=0;};
Bytes payload(const Disc& d,uint32_t lba,uint32_t size){
 if(size>16*1024*1024)fail("Unexpectedly large disc metadata.");
 std::ifstream in(d.file,std::ios::binary);Bytes result(size);uint32_t done=0;
 while(done<size){uint64_t where=(d.first+lba++)*d.sector+d.offset;uint32_t n=std::min(2048u,size-done);
  if(where>fs::file_size(d.file)||n>fs::file_size(d.file)-where)fail("Disc extent is outside the image.");
  in.seekg(where);read(in,result.data()+done,n);done+=n;}
 return result;
}
Disc detect(const fs::path& file){
 for(auto shape:{std::pair<uint32_t,uint32_t>{2352,24},{2352,16},{2048,0}}){
  if(fs::file_size(file)<uint64_t(17)*shape.first)continue;
  Disc d{file,shape.first,shape.second,0};auto v=payload(d,16,2048);
  if(std::string(v.data(),7)==std::string("\x01" "CD001\x01",7))return d;
 }
 fail("This file is not a readable PlayStation ISO image.");return {};
}
void validate_filesystem(const Disc& disc,uint32_t lba,uint32_t size,const std::string& parent,std::set<uint32_t>& visited){
 if(!visited.insert(lba).second)fail("The disc contains a cyclic directory.");
 auto data=payload(disc,lba,size);
 for(size_t p=0;p<data.size();){unsigned n=uint8_t(data[p]);if(!n){p=(p/2048+1)*2048;continue;}
  if(n<34||p+n>data.size()||uint8_t(data[p+32])+33>n)fail("The disc directory is damaged.");
  std::string name(data.data()+p+33,uint8_t(data[p+32]));uint32_t extent=le32(data.data()+p+2),bytes=le32(data.data()+p+10);
  if(name!=std::string(1,'\0') && name!=std::string(1,'\1')){
   if(uint8_t(data[p+25])&2)validate_filesystem(disc,extent,bytes,name,visited);
   else if(parent!="CDDA" && (disc.first+extent+(uint64_t(bytes)+2047)/2048)>fs::file_size(disc.file)/disc.sector)fail("The disc image is incomplete: a game file extends beyond the data track.");
  }p+=n;
 }
}
const RageRegionProfile& region_for_executable(const Bytes& exe){
 Hash h;h.add(exe.data(),exe.size());const auto digest=hex(h.finish());
 for(const auto& region:rage_region_profiles)if(digest==region.exe_sha)return region;
 fail("This Rage Racer revision is not supported. Use Europe (SCES-00650), USA (SLUS-00403), or Japan v1.1 (SLPS-00600).");
 return rage_region_profiles[0];
}
Bytes executable(const Disc& disc){
 auto pvd=payload(disc,16,2048);
 if(std::string(pvd.data(),7)!=std::string("\x01" "CD001\x01",7)||uint8_t(pvd[156])<34)fail("The disc does not contain a valid ISO9660 filesystem.");
 std::set<uint32_t> visited;validate_filesystem(disc,le32(pvd.data()+158),le32(pvd.data()+166),"",visited);
 auto root=payload(disc,le32(pvd.data()+158),le32(pvd.data()+166));
 for(size_t p=0;p<root.size();){unsigned n=uint8_t(root[p]);if(!n){p=(p/2048+1)*2048;continue;}
  if(n<34||p+n>root.size()||uint8_t(root[p+32])+33>n)fail("The disc directory is damaged.");
  std::string name(root.data()+p+33,uint8_t(root[p+32]));
  for(const auto& region:rage_region_profiles)if(name==region.serial||name==std::string(region.serial)+";1"){
   auto exe=payload(disc,le32(root.data()+p+2),le32(root.data()+p+10));
   const auto& actual=region_for_executable(exe);
   if(std::string(actual.serial)!=region.serial)fail("The disc's game identifier and executable do not match.");
   return exe;
  }p+=n;
 }
 fail("Please use a supported Rage Racer disc: Europe, USA, or Japan v1.1.");return {};
}
void copy(const fs::path& src,const fs::path& dst,const std::atomic<bool>& cancel,const std::function<void(uint64_t)>& progress){
 std::ifstream in(src,std::ios::binary);std::ofstream out(dst,std::ios::binary);Bytes b(1024*1024);uint64_t left=fs::file_size(src);
 while(left){check_cancel(cancel);size_t n=size_t(std::min<uint64_t>(b.size(),left));read(in,b.data(),n);write(out,b.data(),n);left-=n;progress(n);}
 out.close();if(!out)fail("Cannot finish writing the disc copy.");
}
void expand_cooked(const fs::path& source,const fs::path& destination,const std::atomic<bool>& cancel,const std::function<void(uint64_t)>& progress){
    std::ifstream in(source,std::ios::binary);std::ofstream out(destination,std::ios::binary);uint64_t size=fs::file_size(source);
    if(size%2048)fail("ISO size is not a whole number of sectors.");
    for(uint64_t i=0;i<size/2048;++i){check_cancel(cancel);std::array<char,2352> raw{};std::fill(raw.begin()+1,raw.begin()+11,char(255));raw[15]=2;
     uint32_t msf=uint32_t(i)+150;auto bcd=[](unsigned n){return char((n/10)*16+n%10);};raw[12]=bcd(msf/(60*75));raw[13]=bcd(msf/75%60);raw[14]=bcd(msf%75);
     read(in,raw.data()+24,2048);write(out,raw.data(),raw.size());if(i%128==0)progress(uint64_t(128)*2048);}
    out.close();if(!out)fail("Cannot finish writing ISO sectors.");
}
std::string cue_time(uint32_t lba){char s[32];std::snprintf(s,sizeof s,"%02u:%02u:%02u",lba/(60*75),(lba/75)%60,lba%75);return s;}
void validate_region_track(const Disc& disc,const GenerateProgress& progress,const std::atomic<bool>& cancel){
 const auto& region=region_for_executable(executable(disc));
 const uint64_t start=disc.first*disc.sector,total=fs::file_size(disc.file);
 if(disc.sector!=2352 || start>total || region.bytes>total-start)fail("The regional data track is incomplete.");
 std::ifstream in(disc.file,std::ios::binary);in.seekg(start);Bytes buffer(1024*1024);uint64_t left=region.bytes;Hash hash;
 while(left){check_cancel(cancel);size_t n=size_t(std::min<uint64_t>(buffer.size(),left));read(in,buffer.data(),n);hash.add(buffer.data(),n);left-=n;}
 const auto digest=hex(hash.finish());
 if(digest!=region.raw_sha && digest!=region.cooked_sha)fail("The data track differs from the verified disc revision. No archive was published.");
 progress(46,L"Verified original disc data. No regional conversion is needed.");
}
std::string disc_config(const RageRegionProfile& region){
 std::string id=region.serial;id.erase(std::remove(id.begin(),id.end(),'.'),id.end());std::replace(id.begin(),id.end(),'_','-');
 return "[game]\nname = \"Rage Racer\"\nid = \""+id+"\"\nplayers = 1\nexe = \"game/"+region.serial+"\"\ndisc = \"disc/game.cue\"\n"
        "[recompiler]\nseeds = \"generated/functions.txt\"\nout_dir = \"generated\"\nstrict = true\n"
        "[runtime]\nwindow_title = \"Rage Racer - PC\"\nmemcard_dir = \"saves\"\noverlay_cache = true\n"
        "[video]\nrenderer = \"opengl\"\naspect_ratio = \"4:3\"\n[controller]\ndefault_mode = \"digital\"\n";
}
struct Handle {HANDLE h=INVALID_HANDLE_VALUE;~Handle(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
void rip_cd(const fs::path& drive,const fs::path& bin,const fs::path& cue,const GenerateProgress& progress,const std::atomic<bool>& cancel){
 std::wstring letter=drive.wstring();if(letter.size()<2||letter[1]!=L':')fail("Select a CD drive.");
 std::wstring device=L"\\\\.\\"+letter.substr(0,2);Handle handle;handle.h=CreateFileW(device.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
 if(handle.h==INVALID_HANDLE_VALUE)fail("Cannot open the CD drive. Close any CD player using it, and check that a disc is inserted.");
 CDROM_TOC toc{};DWORD returned=0;
 if(!DeviceIoControl(handle.h,IOCTL_CDROM_READ_TOC,nullptr,0,&toc,sizeof toc,&returned,nullptr)||toc.FirstTrack!=1||toc.LastTrack<1||toc.LastTrack>99||returned<4+8*(toc.LastTrack+1))fail("Cannot read the CD table of contents.");
 auto lba=[](const TRACK_DATA& t){int v=(t.Address[1]*60+t.Address[2])*75+t.Address[3]-150;if(v<0)fail("Invalid CD track address.");return uint32_t(v);};
 const unsigned tracks=toc.LastTrack;uint32_t end=lba(toc.TrackData[tracks]);
 if(!(toc.TrackData[0].Control&4)||lba(toc.TrackData[0])!=0||!end||end>450000)fail("This is not a supported data CD.");
 std::ostringstream sheet;sheet<<"FILE \"disc.bin\" BINARY\n";
 for(unsigned t=0;t<tracks;++t){char n[16];std::snprintf(n,sizeof n,"%02u",t+1);sheet<<"  TRACK "<<n<<((toc.TrackData[t].Control&4)?" MODE2/2352\n":" AUDIO\n")<<"    INDEX 01 "<<cue_time(lba(toc.TrackData[t]))<<"\n";}
 std::ofstream out(bin,std::ios::binary);std::array<char,2352*16> block{};
 for(unsigned track=0;track<tracks;++track){uint32_t start=lba(toc.TrackData[track]),stop=lba(toc.TrackData[track+1]);if(stop<=start)fail("The CD has invalid track extents.");
  for(uint32_t pos=start;pos<stop;){check_cancel(cancel);DWORD n=std::min(16u,stop-pos);bool ok=false;
   // Some drives require a single-sector read at a data/audio pregap boundary.
   for(int attempt=0;attempt<4&&!ok;++attempt){if(attempt>=2)n=1;RAW_READ_INFO request{};request.DiskOffset.QuadPart=int64_t(pos)*2048;request.SectorCount=n;
    request.TrackMode=(toc.TrackData[track].Control&4)?XAForm2:CDDA;if(attempt&1)request.TrackMode=request.TrackMode==CDDA?XAForm2:CDDA;
    returned=0;ok=DeviceIoControl(handle.h,IOCTL_CDROM_RAW_READ,&request,sizeof request,block.data(),n*2352,&returned,nullptr)&&returned==n*2352;
   }
   if(!ok)fail("The CD drive could not read a sector. Clean the disc or use a verified CUE/BIN image. No unreadable sectors were substituted.");
   write(out,block.data(),n*2352);pos+=n;progress(3+int(uint64_t(pos)*42/end),L"Reading CD track "+std::to_wstring(track+1)+L" of "+std::to_wstring(tracks));
  }
 }
 out.close();if(!out)fail("Cannot finish writing the disc image.");text(cue,sheet.str());
}
}
void generate_data(const GenerateRequest& request,const GenerateProgress& progress,const std::atomic<bool>& cancel){
 check_cancel(cancel);fs::create_directories(request.output);auto target=request.output/L"data.dat";
 if(fs::exists(target)&&!request.replace)fail("data.dat already exists. Choose another folder or confirm replacement.");
 Work work{request.output/(L".generate-data-"+std::to_wstring(GetCurrentProcessId()))};
 if(!fs::create_directory(work.path))fail("A previous generation folder exists. Close the other generator before trying again.");
 work.owned=true;progress(1,L"Checking source...");std::vector<Entry> files;Disc disc;std::string cue;fs::path staged_cue=work.path/L"game.cue";
 if(request.physical){rip_cd(request.input,work.path/L"disc.bin",staged_cue,progress,cancel);disc=detect(work.path/L"disc.bin");files.push_back({"disc/disc.bin",work.path/L"disc.bin"});}
 else {
  std::wstring ext=request.input.extension().wstring();std::transform(ext.begin(),ext.end(),ext.begin(),::towlower);
  if(ext==L".cue"){
   std::ifstream in(request.input,std::ios::binary);if(!in)fail("Cannot open the CUE file.");std::string line;std::ostringstream sheet;std::vector<fs::path> sources;std::vector<std::string> modes;
   int current=-1;bool data_found=false;uint64_t data_index=0;int data_file=-1;std::string mode;
   while(std::getline(in,line)){
    if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.size()>4096)fail("CUE line is too long.");
    if(line.size()>=3&&uint8_t(line[0])==239&&uint8_t(line[1])==187&&uint8_t(line[2])==191)line.erase(0,3);
    std::smatch m;
    if(std::regex_match(line,m,std::regex(R"cue(\s*FILE\s+(?:"([^"]+)"|(\S+))\s+(\S+)\s*)cue",std::regex::icase))){
     if(_stricmp(m[3].str().c_str(),"BINARY"))fail("Only BINARY CUE tracks are supported. Convert compressed audio tracks to a standard BIN/CUE image first.");
     fs::path source=request.input.parent_path()/fs::u8path(m[1].matched?m[1].str():m[2].str());if(!fs::is_regular_file(source))fail("A track referenced by the CUE is missing.");
     current=int(sources.size());sources.push_back(source);modes.push_back("");std::string name="track"+std::to_string(current+1)+".bin";
     sheet<<"FILE \""<<name<<"\" BINARY\n";files.push_back({"disc/"+name,work.path/fs::u8path(name)});continue;
    }
    std::string upper=line;std::transform(upper.begin(),upper.end(),upper.begin(),::toupper);
    int track=0,index=0,mm=0,ss=0,ff=0;char track_mode[32]{};
    if(std::sscanf(upper.c_str()," TRACK %d %31s",&track,track_mode)==2){
     if(current<0||track<1||track>99)fail("Invalid CUE track.");mode=track_mode;std::transform(mode.begin(),mode.end(),mode.begin(),::toupper);
     if(mode!="AUDIO"&&mode!="MODE2/2352"&&mode!="MODE1/2352"&&mode!="MODE1/2048")fail("Unsupported CUE sector format.");
     if(modes[current].empty())modes[current]=mode;
    }
    if(std::sscanf(upper.c_str()," INDEX %d %d:%d:%d",&index,&mm,&ss,&ff)==4&&index==1&&mode!="AUDIO"&&!data_found){
     if(current<0||mm<0||ss<0||ss>=60||ff<0||ff>=75)fail("Invalid CUE data index.");data_found=true;data_file=current;data_index=uint64_t(mm*60+ss)*75+ff;
    }
    sheet<<line<<'\n';
   }
   if(!data_found||sources.empty())fail("The CUE does not contain a data track.");
   auto data_mode=modes[data_file];disc={sources[data_file],data_mode=="MODE1/2048"?2048u:2352u,data_mode=="MODE2/2352"?24u:data_mode=="MODE1/2352"?16u:0u,data_index};
   auto boot=executable(disc); // Reject a wrong game before copying hundreds of MB.
   uint64_t total=0,done=0;for(const auto& f:sources)total+=fs::file_size(f);
   for(size_t i=0;i<sources.size();++i){auto report=[&](uint64_t n){done=std::min(total,done+n);progress(3+int(done*42/std::max<uint64_t>(total,1)),L"Copying disc tracks...");};
    if(modes[i]=="MODE1/2048")expand_cooked(sources[i],files[i].file,cancel,report);else copy(sources[i],files[i].file,cancel,report);
   }
   disc.file=files[data_file].file;if(data_mode=="MODE1/2048"){disc.sector=2352;disc.offset=24;}
   text(staged_cue,std::regex_replace(sheet.str(),std::regex("MODE1/2048",std::regex::icase),"MODE2/2352"));
  }else if(ext==L".iso"){
   disc=detect(request.input);auto boot=executable(disc);auto destination=work.path/L"disc.bin";
   if(disc.sector==2352){uint64_t total=fs::file_size(request.input),done=0;copy(request.input,destination,cancel,[&](uint64_t n){done+=n;progress(3+int(done*42/total),L"Copying ISO image...");});}
   else {
    uint64_t done=0,total=fs::file_size(request.input);expand_cooked(request.input,destination,cancel,[&](uint64_t n){done=std::min(total,done+n);progress(3+int(done*42/total),L"Preparing ISO sectors...");});
   }
   text(staged_cue,"FILE \"disc.bin\" BINARY\n  TRACK 01 "+std::string(disc.offset==16?"MODE1/2352":"MODE2/2352")+"\n    INDEX 01 00:00:00\n");
   disc={destination,2352,disc.offset==16?16u:24u,0};files.push_back({"disc/disc.bin",destination});
  }else fail("Choose a .cue or .iso file, or select a physical CD drive.");
 }
 check_cancel(cancel);progress(46,L"Validating Rage Racer...");validate_region_track(disc,progress,cancel);auto exe=executable(disc);const auto& region=region_for_executable(exe);const auto boot_path=work.path/fs::u8path(region.serial);text(boot_path,std::string(exe.data(),exe.size()));
 const auto audio_files=encode_cdda(staged_cue,progress,cancel);
 if(!audio_files.empty()){
  files.clear();for(const auto& file:audio_files)files.push_back({"disc/"+file.filename().string(),file});
 }
 files.push_back({"disc/game.cue",staged_cue});files.push_back({std::string("game/")+region.serial,boot_path});
 for(const auto& support:rage_support_files){HRSRC r=FindResourceW(nullptr,MAKEINTRESOURCEW(support.id),RT_RCDATA);HGLOBAL h=r?LoadResource(nullptr,r):nullptr;
  if(!h)fail("The generator's support files are damaged. Rebuild or download the generator again.");const char* data=static_cast<const char*>(LockResource(h));DWORD size=SizeofResource(nullptr,r);
  Entry e;e.name=support.name;
  if(e.name=="game.toml"){const auto config=disc_config(region);e.embedded.assign(config.begin(),config.end());}
  else e.embedded.assign(data,data+size);files.push_back(std::move(e));}
 std::sort(files.begin(),files.end(),[](const Entry& a,const Entry& b){return a.name<b.name;});
 uint64_t total=0,done=0;for(auto& f:files){f.size=f.file.empty()?f.embedded.size():fs::file_size(f.file);total+=f.size;}
 auto visit=[&](Entry& e,const std::function<void(const char*,size_t)>& consume){
  if(e.file.empty()){check_cancel(cancel);consume(e.embedded.data(),e.embedded.size());return;}
  std::ifstream in(e.file,std::ios::binary);Bytes b(1024*1024);uint64_t left=e.size;while(left){check_cancel(cancel);size_t n=size_t(std::min<uint64_t>(b.size(),left));read(in,b.data(),n);consume(b.data(),n);left-=n;}
 };
 for(auto& f:files){Hash hash;visit(f,[&](const char* p,size_t n){hash.add(p,n);done+=n;progress(47+int(done*15/total),L"Checking disc data...");});f.digest=hash.finish();}
 uint64_t manifest_size=4;for(auto& f:files)manifest_size+=2+f.name.size()+48;
 uint64_t offset=44+manifest_size;std::ostringstream manifest(std::ios::binary);integer(manifest,files.size(),4);
 for(auto& f:files){f.offset=offset;integer(manifest,f.name.size(),2);write(manifest,f.name.data(),f.name.size());integer(manifest,f.size,8);integer(manifest,offset,8);write(manifest,f.digest.data(),32);offset+=f.size;}
 std::string index=manifest.str();Hash index_hash;index_hash.add(index.data(),index.size());std::string digest=index_hash.finish();auto archive=work.path/L"data.dat";
 {std::ofstream out(archive,std::ios::binary);write(out,"RAGEDAT1",8);integer(out,index.size(),4);write(out,digest.data(),32);write(out,index.data(),index.size());done=0;
  for(auto& f:files){Hash hash;visit(f,[&](const char* p,size_t n){hash.add(p,n);write(out,p,n);done+=n;progress(62+int(done*23/total),L"Writing data.dat...");});if(hash.finish()!=f.digest)fail("Source data changed while generating. Try again.");}
  out.close();if(!out)fail("Cannot finish writing data.dat.");}
 {std::ifstream verify(archive,std::ios::binary);Bytes b(1024*1024);done=0;
  for(const auto& f:files){verify.seekg(f.offset);Hash hash;uint64_t left=f.size;while(left){check_cancel(cancel);size_t n=size_t(std::min<uint64_t>(b.size(),left));read(verify,b.data(),n);hash.add(b.data(),n);left-=n;done+=n;progress(85+int(done*14/total),L"Verifying data.dat...");}if(hash.finish()!=f.digest)fail("The generated archive failed verification.");}
 }
 check_cancel(cancel);
 // Publish only the completely verified file. Never remove an old archive first.
 if(!MoveFileExW(archive.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH|(request.replace?MOVEFILE_REPLACE_EXISTING:0)))fail("Cannot publish data.dat. Close the game and check folder permissions.");
 progress(100,L"Ready. data.dat was generated and verified.");
}

#include "rage_bundle.h"
#include "rage_oval.h"
#include <windows.h>
#include <bcrypt.h>
#include <commctrl.h>
#include <shellapi.h>
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <set>
#include <stdexcept>
#include <cstdio>
#include <sstream>
#include <regex>
#include <cstdlib>
namespace fs = std::filesystem;
namespace {
struct Hash {
    BCRYPT_ALG_HANDLE alg{}; BCRYPT_HASH_HANDLE hash{};
    Hash() {
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) ||
            BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0)) throw std::runtime_error("Cannot initialise data verification");
    }
    ~Hash() { if (hash) BCryptDestroyHash(hash); if (alg) BCryptCloseAlgorithmProvider(alg, 0); }
    void add(const char* p, size_t n) { if (BCryptHashData(hash, (PUCHAR)p, (ULONG)n, 0)) throw std::runtime_error("Data verification failed"); }
    std::string finish() { std::string s(32, '\0'); if (BCryptFinishHash(hash, (PUCHAR)s.data(), 32, 0)) throw std::runtime_error("Data verification failed"); return s; }
};
void read(std::istream& in, char* p, size_t n) { if (!in.read(p, n)) throw std::runtime_error("data.dat is incomplete"); }
uint64_t integer(std::istream& in, unsigned n) { char b[8]; read(in,b,n); uint64_t v=0; for(unsigned i=0;i<n;++i) v|=(uint64_t)(unsigned char)b[i]<<(i*8); return v; }
std::string hex(const std::string& s) { std::string r; const char* h="0123456789abcdef"; for(unsigned char c:s) {r+=h[c>>4];r+=h[c&15];} return r; }
struct Entry { fs::path name; uint64_t size, offset; std::string digest; };
}
int rage_bundle_prepare(int& argc, char**& argv) {
    bool extract_only=false, verify=false;
    for(int i=1;i<argc;++i) {
        if(std::string(argv[i])=="--game") return -1;
        extract_only |= std::string(argv[i])=="--bundle-extract-only";
        verify |= std::string(argv[i])=="--bundle-verify";
    }
    wchar_t module[32768]; DWORD len=GetModuleFileNameW(nullptr,module,32768);
    if(!len || len==32768) return 1;
    const fs::path folder=fs::path(std::wstring(module,len)).parent_path();
    const fs::path archive=folder/"data.dat";
    if(!fs::exists(archive)) {
        if(extract_only || verify) return 1;
        const auto filename=fs::path(std::wstring(module,len)).filename();
        if(filename==L"RageRacer.exe" || filename==L"Rage_Racer.exe") {
            TASKDIALOG_BUTTON buttons[]={{IDYES,L"Generate data..."},{IDCANCEL,L"Close"}};
            TASKDIALOGCONFIG dialog{sizeof dialog};dialog.pszWindowTitle=L"Rage Racer - PC";
            dialog.pszMainInstruction=L"Game data is needed";
            dialog.pszContent=L"data.dat was not found beside RageRacer.exe. Generate it from your original Rage Racer CD or a disc image (Europe, USA or Japan v1.1).";
            dialog.cButtons=2;dialog.pButtons=buttons;dialog.nDefaultButton=IDYES;dialog.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION;
            int chosen=IDCANCEL;
            if(FAILED(TaskDialogIndirect(&dialog,&chosen,nullptr,nullptr)))
                chosen=MessageBoxW(nullptr,L"data.dat was not found. Open Generate Data to create it from your original CD or disc image?",L"Rage Racer - PC",MB_YESNO|MB_ICONINFORMATION);
            if(chosen==IDYES){
                const auto generator=folder/L"Generate Data.exe";
                const auto arguments=L"--output \""+folder.wstring()+L"\"";
                if(!fs::is_regular_file(generator) || (INT_PTR)ShellExecuteW(nullptr,L"open",generator.c_str(),arguments.c_str(),folder.c_str(),SW_SHOWNORMAL)<=32)
                    MessageBoxW(nullptr,L"Cannot open Generate Data.exe. Keep it in the same folder as RageRacer.exe.",L"Rage Racer - PC",MB_OK|MB_ICONERROR);
            }
            return 0;
        }
        return -1;
    }
    fs::path staging;
    try {
        std::ifstream in(archive,std::ios::binary);
        char magic[8]; read(in,magic,8);
        if(std::string(magic,8)!="RAGEDAT1") throw std::runtime_error("Unsupported data.dat format");
        const auto manifest_size=integer(in,4);
        if(!manifest_size || manifest_size>4*1024*1024) throw std::runtime_error("Invalid data.dat index");
        std::string digest(32,'\0'); read(in,digest.data(),32);
        std::string manifest((size_t)manifest_size,'\0'); read(in,manifest.data(),manifest.size());
        Hash index_hash; index_hash.add(manifest.data(),manifest.size());
        if(index_hash.finish()!=digest) throw std::runtime_error("data.dat index checksum failed");
        std::istringstream index(manifest,std::ios::binary);
        auto count=integer(index,4);
        if(!count || count>4096) throw std::runtime_error("Invalid data.dat file count");
        std::vector<Entry> entries; std::set<std::string> names;
        uint64_t expected_offset=44+manifest_size;
        for(uint64_t i=0;i<count;++i) {
            const auto n=integer(index,2);
            if(!n || n>1024) throw std::runtime_error("Invalid bundle filename");
            std::string name((size_t)n,'\0'); read(index,name.data(),name.size());
            if(name.find('\\')!=std::string::npos || name.find(':')!=std::string::npos || name.find('\0')!=std::string::npos) throw std::runtime_error("Unsafe bundle filename");
            fs::path path=fs::u8path(name);
            if(path.is_absolute()) throw std::runtime_error("Unsafe bundle filename");
            for(const auto& part:path) if(part==".." || part==".") throw std::runtime_error("Unsafe bundle filename");
            if(!names.insert(name).second) throw std::runtime_error("Duplicate bundle filename");
            Entry e{path,integer(index,8),integer(index,8),std::string(32,'\0')}; read(index,e.digest.data(),32);
            if(e.offset!=expected_offset || expected_offset>fs::file_size(archive) || e.size>fs::file_size(archive)-expected_offset) throw std::runtime_error("Invalid bundle file extent");
            expected_offset+=e.size; entries.push_back(e);
        }
        if(index.peek()!=EOF || expected_offset!=fs::file_size(archive)) throw std::runtime_error("Invalid bundle length");
        // Derived files and saves are created locally.
        fs::path root=folder/".runtime";
        if(const char* test_root=std::getenv("RAGE_BUNDLE_CACHE")) root=fs::path(test_root);
        const auto cache=root/hex(digest);
        bool ready=fs::exists(cache/".complete");
        for(const auto& e:entries) if(!fs::exists(cache/e.name) || fs::file_size(cache/e.name)!=e.size) ready=false;
        if(!ready || verify) {
            fs::create_directories(root);
            staging=root/(hex(digest)+".tmp-"+std::to_string(GetCurrentProcessId()));
            if(fs::exists(staging)) throw std::runtime_error("Bundle extraction is already in progress");
            fs::create_directory(staging);
            std::vector<char> buffer(1024*1024);
            for(const auto& e:entries) {
                fs::create_directories((staging/e.name).parent_path());
                std::ofstream out(staging/e.name,std::ios::binary); Hash hash;
                in.seekg(e.offset); uint64_t left=e.size;
                while(left) { size_t n=(size_t)(left<buffer.size()?left:buffer.size()); read(in,buffer.data(),n); hash.add(buffer.data(),n); out.write(buffer.data(),n); left-=n; }
                out.close(); if(!out || hash.finish()!=e.digest) throw std::runtime_error("Bundle file checksum failed: "+e.name.string());
            }
            std::ofstream(staging/".complete")<<hex(digest);
            if(ready) { fs::remove_all(staging); staging.clear(); }
            else {
                if(fs::exists(cache)) throw std::runtime_error("Local runtime cache is incomplete; remove .runtime and relaunch");
                // Windows scanners can briefly hold a just-extracted file
                // without FILE_SHARE_DELETE, preventing the directory commit.
                // Retry only transient sharing/access errors; never replace a
                // competing cache or publish an incomplete extraction.
                std::error_code commit_error;
                for (int attempt=0; attempt<100; ++attempt) {
                    fs::rename(staging,cache,commit_error);
                    if (!commit_error) break;
                    if (fs::exists(cache) || (commit_error.value()!=ERROR_ACCESS_DENIED &&
                        commit_error.value()!=ERROR_SHARING_VIOLATION)) break;
                    Sleep(100);
                }
                if (commit_error) throw fs::filesystem_error("Cannot commit verified runtime cache",staging,cache,commit_error);
                staging.clear();
            }
        }
        std::printf("Rage Racer bundle verified: %zu files\n",entries.size());
        if(extract_only || verify) return 0;
        // Resolve the verified bundle's executable against its own directory.
        // Project discovery can otherwise climb into a developer checkout and
        // resolve a portable game/ path against that unrelated project root.
        std::ifstream config_input(cache/"game.toml",std::ios::binary);
        std::ostringstream config_output;std::string config_line;bool game_section=false,exe_found=false;fs::path original_executable;
        while(std::getline(config_input,config_line)) {
            std::smatch field;
            if(std::regex_match(config_line,field,std::regex(R"(^\s*\[([A-Za-z0-9_.]+)\]\s*$)")))game_section=field[1].str()=="game";
            if(game_section && std::regex_match(config_line,field,std::regex(R"cfg(^\s*exe\s*=\s*"([^"]+)"\s*$)cfg"))) {
                const fs::path relative=fs::u8path(field[1].str());
                if(relative.is_absolute())throw std::runtime_error("Bundle executable must be a relative path");
                for(const auto& part:relative)if(part=="..")throw std::runtime_error("Unsafe bundle executable path");
                original_executable=cache/relative;
                config_line="exe = \""+(cache/relative).generic_u8string()+"\"";exe_found=true;
            }
            config_output<<config_line<<'\n';
        }
        if(!exe_found)throw std::runtime_error("Bundle config is missing its game executable");
        const fs::path runtime_config=cache/"runtime-game.toml";
        {std::ofstream prepared(runtime_config,std::ios::binary);prepared<<config_output.str();prepared.close();
         if(!prepared)throw std::runtime_error("Cannot prepare local game configuration");}
        rage_oval_prepare(cache/"disc/game.cue",original_executable);
        fs::create_directories(folder/"saves"); fs::current_path(cache);
        static std::vector<std::string> args; static std::vector<char*> pointers;
        args.push_back(argv[0]);
        args.insert(args.end(),{"--game",runtime_config.string(),"--disc",(cache/"disc/game.cue").string(),"--bios",(cache/"bios/openbios.bin").string(),"--memcard-dir",(folder/"saves").string(),"--no-launcher"});
        for(int i=1;i<argc;++i) args.push_back(argv[i]);
        for(auto& arg:args) pointers.push_back(arg.data()); pointers.push_back(nullptr);
        argc=(int)args.size(); argv=pointers.data(); return -1;
    } catch(const std::exception& e) {
        if(!staging.empty()) { std::error_code ignored; fs::remove_all(staging,ignored); }
        std::fprintf(stderr,"Portable bundle: %s\n",e.what());
        if(!extract_only && !verify) MessageBoxA(nullptr,e.what(),"Rage Racer data error",MB_OK|MB_ICONERROR);
        return 1;
    }
}

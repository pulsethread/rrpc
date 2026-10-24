#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <vector>

extern "C" int rage_stats_enabled();
extern "C" uint64_t rage_native_cache_usage();
namespace {
std::atomic<uint64_t> presents{0};
uint64_t file_time(FILETIME t){return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;}
struct StatsBitmap {
    static constexpr int width=324,height=126;
    HDC dc=nullptr;HBITMAP bitmap=nullptr;HFONT font=nullptr;void* pixels=nullptr;
    PDH_HQUERY query=nullptr;PDH_HCOUNTER gpu=nullptr;
    uint64_t sample_time=0,last_frames=0,last_cpu=0;
    bool active=false,gpu_attempted=false;
    ~StatsBitmap(){if(query)PdhCloseQuery(query);if(dc)DeleteDC(dc);if(bitmap)DeleteObject(bitmap);if(font)DeleteObject(font);}
    bool initialize(){
        if(dc)return true;
        dc=CreateCompatibleDC(nullptr);if(!dc)return false;
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        if(!bitmap){DeleteDC(dc);dc=nullptr;return false;}
        SelectObject(dc,bitmap);
        font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,FIXED_PITCH,L"Consolas");
        if(font)SelectObject(dc,font);
        SetTextColor(dc,RGB(255,255,255));SetBkMode(dc,TRANSPARENT);
        return true;
    }
    double gpu_usage(){
        if(!gpu_attempted){
            gpu_attempted=true;
            wchar_t path[160];swprintf_s(path,L"\\GPU Engine(pid_%lu_*engtype_3D)\\Utilization Percentage",GetCurrentProcessId());
            if(PdhOpenQueryW(nullptr,0,&query)!=ERROR_SUCCESS)return -1;
            if(PdhAddEnglishCounterW(query,path,0,&gpu)!=ERROR_SUCCESS){PdhCloseQuery(query);query=nullptr;return -1;}
            PdhCollectQueryData(query);return -1;
        }
        if(!query || PdhCollectQueryData(query)!=ERROR_SUCCESS)return -1;
        DWORD bytes=0,count=0;
        if(PdhGetFormattedCounterArrayW(gpu,PDH_FMT_DOUBLE,&bytes,&count,nullptr)!=PDH_MORE_DATA)return -1;
        std::vector<unsigned char> storage(bytes);
        auto* items=reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(storage.data());
        if(PdhGetFormattedCounterArrayW(gpu,PDH_FMT_DOUBLE,&bytes,&count,items)!=ERROR_SUCCESS)return -1;
        double usage=-1;
        for(DWORD i=0;i<count;++i)if(items[i].FmtValue.CStatus==PDH_CSTATUS_VALID_DATA || items[i].FmtValue.CStatus==PDH_CSTATUS_NEW_DATA)
            usage=std::max(usage,items[i].FmtValue.doubleValue);
        return usage<0?-1:std::clamp(usage,0.,100.);
    }
    const uint32_t* image(){
        if(!rage_stats_enabled()){active=false;return nullptr;}
        if(!initialize())return nullptr;
        const uint64_t now=GetTickCount64(),frames=presents.load(std::memory_order_relaxed);
        if(active && now-sample_time<500)return static_cast<uint32_t*>(pixels);
        FILETIME creation{},exit{},kernel{},user{};double cpu=-1;
        uint64_t cpu_time=0;
        if(GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user)){
            cpu_time=file_time(kernel)+file_time(user);
            if(active && now>sample_time){const DWORD processors=GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
                cpu=std::clamp(double(cpu_time-last_cpu)/double((now-sample_time)*10000)/std::max(1ul,processors)*100.,0.,100.);}
        }
        const double fps=active && now>sample_time?double(frames-last_frames)*1000/(now-sample_time):0;
        const double gpu_percent=gpu_usage();char text[512],cpu_text[40],gpu_text[40];
        if(cpu>=0)std::snprintf(cpu_text,sizeof cpu_text,"%.1f%% (game)",cpu);else std::snprintf(cpu_text,sizeof cpu_text,"N/A");
        if(gpu_percent>=0)std::snprintf(gpu_text,sizeof gpu_text,"%.1f%% (3D)",gpu_percent);else std::snprintf(gpu_text,sizeof gpu_text,"N/A");
        std::snprintf(text,sizeof text,"Framerate     %.1f FPS\nFrame time    %.2f ms\nCache usage   %.1f / 32 MiB\nCPU usage     %s\nGPU usage     %s",
            fps,fps>0?1000/fps:0,double(rage_native_cache_usage())/(1024*1024),cpu_text,gpu_text);
        RECT full{0,0,width,height};FillRect(dc,&full,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        RECT content{10,8,width-8,height-6};DrawTextA(dc,text,-1,&content,DT_LEFT|DT_TOP|DT_NOPREFIX);
        GdiFlush();auto* rgba=static_cast<uint32_t*>(pixels);
        for(int i=0;i<width*height;++i){const unsigned luminance=rgba[i]&255;rgba[i]=(rgba[i]&0xffffff)|((170+85*luminance/255)<<24);}
        sample_time=now;last_frames=frames;last_cpu=cpu_time;active=true;
        return rgba;
    }
};
}
extern "C" void rage_stats_note_present(){if(rage_stats_enabled())presents.fetch_add(1,std::memory_order_relaxed);}
extern "C" const uint32_t* rage_stats_image(int* width,int* height){
    static StatsBitmap bitmap;*width=StatsBitmap::width;*height=StatsBitmap::height;return bitmap.image();
}

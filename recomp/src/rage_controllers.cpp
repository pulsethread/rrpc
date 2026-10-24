#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "rage_controllers.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
namespace {
const char* keys[]={"up","down","left","right","cross","circle","square","triangle","l1","r1","l2","r2","select","start"};
const wchar_t* names[]={L"Up",L"Down",L"Left",L"Right",L"Cross",L"Circle",L"Square",L"Triangle",L"L1",L"R1",L"L2",L"R2",L"Select",L"Start"};
const char* sources[]={"none","dpup","dpdown","dpleft","dpright","a","b","x","y","leftshoulder","rightshoulder","lefttrigger+","righttrigger+","back","start","leftstick","rightstick","leftx-","leftx+","lefty-","lefty+"};
struct Seat {std::string device;std::array<std::string,14> binds;};
Seat draft[2];int seat=0;HWND dialog=nullptr,seatbox=nullptr,devicebox=nullptr,bindings[14]{};
std::vector<std::string> devices;std::filesystem::path config;
std::wstring wide(const char* t){int n=MultiByteToWideChar(CP_UTF8,0,t,-1,nullptr,0);std::wstring r(n,L' ');if(n)MultiByteToWideChar(CP_UTF8,0,t,-1,r.data(),n);if(n)r.pop_back();return r;}
void save(){
    if(config.empty())return;auto temp=config;temp+=L".tmp";
    {std::ofstream f(temp);for(int s=0;s<2;++s){f<<"device"<<s<<"="<<draft[s].device<<'\n';for(int i=0;i<14;++i)f<<s<<"."<<keys[i]<<"="<<draft[s].binds[i]<<'\n';}f.flush();if(!f)return;}
    MoveFileExW(temp.c_str(),config.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
void refresh(){
    SendMessageW(devicebox,CB_SETCURSEL,0,0);
    for(size_t i=0;i<devices.size();++i)if(devices[i]==draft[seat].device){SendMessageW(devicebox,CB_SETCURSEL,i,0);break;}
    for(int k=0;k<14;++k){int found=-1;for(int j=0;j<int(std::size(sources));++j)if(draft[seat].binds[k]==sources[j])found=j;
        if(found<0){auto t=wide(draft[seat].binds[k].c_str());found=int(SendMessageW(bindings[k],CB_ADDSTRING,0,(LPARAM)t.c_str()));}
        SendMessageW(bindings[k],CB_SETCURSEL,found,0);
    }
    InvalidateRect(dialog,nullptr,TRUE);
}
void collect(){
    int d=int(SendMessageW(devicebox,CB_GETCURSEL,0,0));if(d>=0 && d<int(devices.size()))draft[seat].device=devices[d];
    for(int k=0;k<14;++k){int j=int(SendMessageW(bindings[k],CB_GETCURSEL,0,0));if(j>=0 && j<int(std::size(sources)))draft[seat].binds[k]=sources[j];}
}
HWND child(const wchar_t* type,const wchar_t* label,DWORD style,int x,int y,int w,int h,int id){auto c=CreateWindowW(type,label,WS_CHILD|WS_VISIBLE|style,x,y,w,h,dialog,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);return c;}
LRESULT CALLBACK proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_COMMAND){int id=LOWORD(wp);
        if(id==100 && HIWORD(wp)==CBN_SELCHANGE){collect();seat=int(SendMessageW(seatbox,CB_GETCURSEL,0,0));refresh();return 0;}
        if((id>=200 && id<214) || id==101){collect();InvalidateRect(w,nullptr,TRUE);return 0;}
        if(id==IDOK){collect();if(draft[0].device=="keyboard" && draft[1].device=="keyboard"){MessageBoxW(w,L"Assign the keyboard to one controller only. Choose a gamepad for the other controller.",L"Controller assignment",MB_OK);return 0;}
            for(int s=0;s<2;++s){rage_host_assign(s,draft[s].device.c_str());for(int k=0;k<14;++k)rage_host_binding(s,keys[k],draft[s].binds[k].c_str());}save();DestroyWindow(w);return 0;}
        if(id==IDCANCEL){DestroyWindow(w);return 0;}
    }
    if(msg==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);SetBkMode(dc,TRANSPARENT);SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));
        const POINT outline[]={{70,218},{82,175},{120,157},{165,165},{194,185},{264,185},{293,165},{338,157},{376,175},{388,218},{410,326},{394,350},{366,351},{323,279},{297,253},{161,253},{135,279},{92,351},{64,350},{48,326}};
        auto grey=CreateSolidBrush(RGB(194,194,188));SelectObject(dc,grey);Polygon(dc,outline,int(std::size(outline)));SelectObject(dc,GetStockObject(DC_BRUSH));SetDCBrushColor(dc,RGB(47,47,48));
        Rectangle(dc,108,201,150,215);Rectangle(dc,122,187,136,229);
        const POINT faces[]={{324,188},{346,210},{324,232},{302,210}};
        for(auto p:faces)Ellipse(dc,p.x-9,p.y-9,p.x+9,p.y+9);
        Rectangle(dc,199,208,219,214);POINT start[]={{238,205},{252,211},{238,217}};Polygon(dc,start,3);
        Rectangle(dc,100,145,151,160);Rectangle(dc,303,145,354,160);
        SelectObject(dc,GetStockObject(NULL_BRUSH));
        const COLORREF colours[]={RGB(70,145,70),RGB(190,75,75),RGB(75,105,190),RGB(180,95,155)};
        for(int i=0;i<4;++i){auto p=faces[i];auto pen=CreatePen(PS_SOLID,2,colours[i]);auto old=SelectObject(dc,pen);
            if(i==0){POINT t[]={{p.x,p.y-5},{p.x-5,p.y+4},{p.x+5,p.y+4},{p.x,p.y-5}};Polyline(dc,t,4);}
            if(i==1)Ellipse(dc,p.x-5,p.y-5,p.x+5,p.y+5);
            if(i==2){MoveToEx(dc,p.x-4,p.y-4,nullptr);LineTo(dc,p.x+4,p.y+4);MoveToEx(dc,p.x-4,p.y+4,nullptr);LineTo(dc,p.x+4,p.y-4);}
            if(i==3)Rectangle(dc,p.x-4,p.y-4,p.x+4,p.y+4);
            SelectObject(dc,old);DeleteObject(pen);
        }
        DeleteObject(grey);
        auto label=[&](int x,int y,const std::wstring& t){TextOutW(dc,x,y,t.c_str(),int(t.size()));};
        label(62,107,L"PS1 digital controller - mappings for this player");
        label(65,370,L"D-pad: navigate / steer");label(65,393,L"Cross: confirm / accelerate    Square: brake");label(65,416,L"Triangle: back    Start: pause");
        label(65,450,L"Gamepad mappings are independent for Controller 1 and 2.");
        label(65,473,L"Keyboard uses the existing keyboard controls.");
        label(105,126,L"L1 / L2");label(306,126,L"R1 / R2");label(94,241,L"D-pad");label(190,234,L"Select / Start");label(302,255,L"Face buttons");
        EndPaint(w,&ps);return 0;
    }
    if(msg==WM_CLOSE){DestroyWindow(w);return 0;}return DefWindowProcW(w,msg,wp,lp);
}
}
void rage_controllers_configure(const char* directory){
    config=std::filesystem::u8path(directory)/"controllers.ini";
    std::ifstream f(config);std::string line;while(std::getline(f,line)){auto p=line.find('=');if(p==std::string::npos)continue;auto key=line.substr(0,p),v=line.substr(p+1);
        for(int s=0;s<2;++s){if(key=="device"+std::to_string(s))rage_host_assign(s,v.c_str());for(auto k:keys)if(key==std::to_string(s)+"."+k)rage_host_binding(s,k,v.c_str());}}
}
void rage_controllers_show(void* parent){
    for(int s=0;s<2;++s){draft[s].device=rage_host_assignment(s);for(int k=0;k<14;++k)draft[s].binds[k]=rage_host_mapping(s,keys[k]);}seat=0;
    WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"RageControllerSetup";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);RegisterClassW(&cls);
    dialog=CreateWindowExW(WS_EX_DLGMODALFRAME,cls.lpszClassName,L"Controllers",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU, CW_USEDEFAULT,CW_USEDEFAULT,900,590,(HWND)parent,nullptr,cls.hInstance,nullptr);
    child(L"STATIC",L"Player",0,24,22,65,22,0);seatbox=child(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,90,18,180,180,100);
    for(auto t:{L"Controller 1",L"Controller 2"})SendMessageW(seatbox,CB_ADDSTRING,0,(LPARAM)t);SendMessageW(seatbox,CB_SETCURSEL,0,0);
    child(L"STATIC",L"Device",0,300,22,65,22,0);devicebox=child(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,365,18,480,250,101);
    devices={"none","keyboard","auto"};for(auto t:{L"Not connected",L"Keyboard",L"Automatic gamepad"})SendMessageW(devicebox,CB_ADDSTRING,0,(LPARAM)t);
    for(int i=0;i<rage_host_device_count();++i){devices.push_back(rage_host_device_guid(i));auto name=wide(rage_host_device_name(i));SendMessageW(devicebox,CB_ADDSTRING,0,(LPARAM)name.c_str());}
    for(int k=0;k<14;++k){child(L"STATIC",names[k],0,485,72+k*29,90,22,0);bindings[k]=child(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,590,68+k*29,250,230,200+k);for(auto source:sources){auto name=wide(source);SendMessageW(bindings[k],CB_ADDSTRING,0,(LPARAM)name.c_str());}}
    child(L"BUTTON",L"Save",BS_DEFPUSHBUTTON|WS_TABSTOP,650,505,90,28,IDOK);child(L"BUTTON",L"Cancel",WS_TABSTOP,750,505,90,28,IDCANCEL);refresh();
    EnableWindow((HWND)parent,FALSE);ShowWindow(dialog,SW_SHOW);MSG msg;while(IsWindow(dialog) && GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(dialog,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}EnableWindow((HWND)parent,TRUE);SetForegroundWindow((HWND)parent);
}

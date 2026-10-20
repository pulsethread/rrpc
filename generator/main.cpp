#define NOMINMAX
#include "generate_data.h"
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <thread>
#include <mutex>
#include <fstream>
#include <chrono>
#include <vector>
#include <memory>
#include <cstdio>
#include <stdexcept>
namespace fs=std::filesystem;
namespace {
HWND window,input,drives,output,status,progress,generate,cancel_button,run_button,image_radio,drive_radio,browse_input,browse_output;
HFONT font,title_font;HBRUSH background;std::thread worker;std::atomic<bool> cancelled{false},notice{false};std::mutex state_lock;
std::wstring progress_text,result_text;int percent=0;bool running=false,closing=false,success=false;int scale=100;
fs::path directory(){wchar_t path[32768];DWORD n=GetModuleFileNameW(nullptr,path,32768);return fs::path(std::wstring(path,n)).parent_path();}
std::wstring get(HWND h){int n=GetWindowTextLengthW(h);std::wstring s(n+1,0);GetWindowTextW(h,s.data(),n+1);s.resize(n);return s;}
void launch_game(){auto exe=directory()/L"RageRacer.exe";if((INT_PTR)ShellExecuteW(window,L"open",exe.c_str(),nullptr,directory().c_str(),SW_SHOWNORMAL)<=32)MessageBoxW(window,L"Cannot open RageRacer.exe. Keep it beside Generate Data.exe.",L"Rage Racer - PC",MB_OK|MB_ICONERROR);}
int px(int v){return MulDiv(v,scale,100);}
HWND control(const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id=0){
 HWND c=CreateWindowExW(cls==std::wstring(L"EDIT")?WS_EX_CLIENTEDGE:0,cls,text,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(w),px(h),window,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE);return c;
}
void source_mode(){bool image=SendMessageW(image_radio,BM_GETCHECK,0,0)==BST_CHECKED;EnableWindow(input,!running&&image);EnableWindow(browse_input,!running&&image);EnableWindow(drives,!running&&!image);}
void enable_idle(){EnableWindow(generate,!running);EnableWindow(browse_output,!running);EnableWindow(image_radio,!running);EnableWindow(drive_radio,!running);source_mode();SetWindowTextW(cancel_button,running?L"Cancel":L"Close");}
void progress_changed(int p,const std::wstring& text){
 {std::lock_guard<std::mutex> lock(state_lock);percent=p;progress_text=text;}
 if(!notice.exchange(true))PostMessageW(window,WM_APP+1,0,0);
}
void browse_file(){wchar_t path[32768]{};OPENFILENAMEW dialog{sizeof dialog};dialog.hwndOwner=window;dialog.lpstrFilter=L"Disc images (*.cue;*.iso)\0*.cue;*.iso\0All files\0*.*\0";dialog.lpstrFile=path;dialog.nMaxFile=32768;dialog.lpstrTitle=L"Choose your Rage Racer disc image";dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_EXPLORER|OFN_NOCHANGEDIR;if(GetOpenFileNameW(&dialog))SetWindowTextW(input,path);}
void browse_folder(){IFileOpenDialog* dialog=nullptr;if(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))){
 DWORD options=0;dialog->GetOptions(&options);dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);dialog->SetTitle(L"Choose where to create data.dat");
 if(SUCCEEDED(dialog->Show(window))){IShellItem* item=nullptr;if(SUCCEEDED(dialog->GetResult(&item))){PWSTR name=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&name))){SetWindowTextW(output,name);CoTaskMemFree(name);}item->Release();}}dialog->Release();}}
void begin(){GenerateRequest request;request.physical=SendMessageW(drive_radio,BM_GETCHECK,0,0)==BST_CHECKED;request.output=get(output);
 if(request.physical){int selected=int(SendMessageW(drives,CB_GETCURSEL,0,0));if(selected==CB_ERR){MessageBoxW(window,L"Insert your game CD and select its drive.",L"Select a CD drive",MB_OK|MB_ICONINFORMATION);return;}wchar_t name[128]{};SendMessageW(drives,CB_GETLBTEXT,selected,(LPARAM)name);request.input=std::wstring(name,2);}
 else {request.input=get(input);if(request.input.empty()){browse_file();request.input=get(input);if(request.input.empty())return;}
  std::wstring ext=request.input.extension().wstring();for(auto& c:ext)c=towlower(c);
  if(ext==L".iso"&&MessageBoxW(window,L"A data-only ISO does not contain the CD music tracks, and may omit video/audio sector data. A complete CUE/BIN image or the original CD is recommended.\n\nContinue with this ISO?",L"ISO image",MB_OKCANCEL|MB_ICONINFORMATION)!=IDOK)return;}
 if(request.output.empty())return;
 if(fs::exists(request.output/L"data.dat")){if(MessageBoxW(window,L"This folder already contains data.dat. Replace it after the new archive has been fully verified?",L"Replace data.dat",MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)!=IDYES)return;request.replace=true;}
 running=true;success=false;cancelled=false;ShowWindow(run_button,SW_HIDE);SendMessageW(progress,PBM_SETPOS,0,0);enable_idle();SetWindowTextW(status,L"Preparing...");
 worker=std::thread([request]{
  try {auto last=std::chrono::steady_clock::now();int previous=-1;generate_data(request,[&](int p,const std::wstring& text){auto now=std::chrono::steady_clock::now();if(p!=previous||now-last>std::chrono::milliseconds(80)){progress_changed(p,text);previous=p;last=now;}},cancelled);{std::lock_guard<std::mutex> lock(state_lock);success=true;result_text=L"Ready to play. data.dat has been generated and verified.";}}
  catch(const std::exception& e){std::string t=e.what();std::lock_guard<std::mutex> lock(state_lock);result_text=std::wstring(t.begin(),t.end());success=false;}
  PostMessageW(window,WM_APP+2,0,0);
 });
}
LRESULT CALLBACK procedure(HWND h,UINT message,WPARAM w,LPARAM l){switch(message){
 case WM_CREATE:{window=h;
  control(L"STATIC",L"Rage Racer - PC",0,28,22,610,36);HWND title=GetWindow(h,GW_CHILD);SendMessageW(title,WM_SETFONT,(WPARAM)title_font,TRUE);
  control(L"STATIC",L"Create game data from your disc: Europe, USA or Japan (v1.1).",0,28,65,610,24);
  image_radio=control(L"BUTTON",L"Disc image",BS_AUTORADIOBUTTON|WS_GROUP|WS_TABSTOP,28,107,155,25,100);SendMessageW(image_radio,BM_SETCHECK,BST_CHECKED,0);
  drive_radio=control(L"BUTTON",L"Physical CD",BS_AUTORADIOBUTTON|WS_TABSTOP,205,107,155,25,101);
  input=control(L"EDIT",L"",ES_AUTOHSCROLL|ES_READONLY|WS_TABSTOP,28,143,512,30,102);browse_input=control(L"BUTTON",L"Browse...",WS_TABSTOP,550,141,104,33,103);
  drives=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,370,107,284,220,104);
  wchar_t letters[512]{};GetLogicalDriveStringsW(511,letters);for(wchar_t* p=letters;*p;p+=wcslen(p)+1)if(GetDriveTypeW(p)==DRIVE_CDROM)SendMessageW(drives,CB_ADDSTRING,0,(LPARAM)p);SendMessageW(drives,CB_SETCURSEL,0,0);
  control(L"STATIC",L"Choose a .cue or .iso file. CUE images keep the available CD music.",0,28,181,626,24);
  control(L"STATIC",L"Create data.dat in",0,28,225,610,22);
  output=control(L"EDIT",directory().c_str(),ES_AUTOHSCROLL|ES_READONLY|WS_TABSTOP,28,252,512,30,105);browse_output=control(L"BUTTON",L"Browse...",WS_TABSTOP,550,250,104,33,106);
  progress=control(PROGRESS_CLASSW,L"",PBS_SMOOTH,28,313,626,14,107);SendMessageW(progress,PBM_SETRANGE32,0,100);
  status=control(L"STATIC",L"Select your source, then choose Generate data.",0,28,339,626,47,108);
  generate=control(L"BUTTON",L"Generate data",BS_DEFPUSHBUTTON|WS_TABSTOP,402,404,142,36,109);cancel_button=control(L"BUTTON",L"Close",WS_TABSTOP,554,404,100,36,110);
  run_button=control(L"BUTTON",L"Play game",WS_TABSTOP,28,404,135,36,111);ShowWindow(run_button,SW_HIDE);source_mode();return 0;}
 case WM_COMMAND:switch(LOWORD(w)){case 100:case 101:source_mode();break;case 103:browse_file();break;case 106:browse_folder();break;case 109:if(!running){try{begin();}catch(const std::exception& e){MessageBoxA(window,e.what(),"Generate Data",MB_OK|MB_ICONERROR);}}break;case 110:SendMessageW(h,WM_CLOSE,0,0);break;case 111:launch_game();break;}return 0;
 case WM_APP+1:{std::lock_guard<std::mutex> lock(state_lock);notice=false;SendMessageW(progress,PBM_SETPOS,percent,0);SetWindowTextW(status,progress_text.c_str());return 0;}
 case WM_APP+2:if(worker.joinable())worker.join();running=false;enable_idle();{std::lock_guard<std::mutex> lock(state_lock);SetWindowTextW(status,result_text.c_str());if(success){SendMessageW(progress,PBM_SETPOS,100,0);std::error_code ec;if(fs::equivalent(fs::path(get(output)),directory(),ec))ShowWindow(run_button,SW_SHOW);}}
  if(closing)DestroyWindow(h);return 0;
 case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:SetBkMode((HDC)w,TRANSPARENT);SetTextColor((HDC)w,RGB(32,43,57));return (LRESULT)background;
 case WM_CLOSE:if(running){cancelled=true;closing=true;SetWindowTextW(status,L"Cancelling safely...");EnableWindow(cancel_button,FALSE);}else DestroyWindow(h);return 0;
 case WM_DESTROY:PostQuitMessage(0);return 0;
 }return DefWindowProcW(h,message,w,l);}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
 int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);GenerateRequest request;bool headless=false;
 for(int i=1;i<argc;++i){std::wstring arg=argv[i];if(arg==L"--headless")headless=true;else if((arg==L"--input"||arg==L"--drive")&&i+1<argc){request.physical=arg==L"--drive";request.input=argv[++i];}else if(arg==L"--output"&&i+1<argc)request.output=argv[++i];else if(arg==L"--replace")request.replace=true;}
 LocalFree(argv);
 if(headless){try{if(request.output.empty()||request.input.empty())throw std::runtime_error("--input/--drive and --output are required.");std::atomic<bool> cancel{false};generate_data(request,[](int,const std::wstring&){},cancel);return 0;}catch(const std::exception& e){std::fprintf(stderr,"Generate Data: %s\n",e.what());OutputDebugStringA(e.what());return 1;}}
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);scale=MulDiv(GetDpiForSystem(),100,96);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 INITCOMMONCONTROLSEX common{sizeof common,ICC_PROGRESS_CLASS|ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
 font=CreateFontW(-px(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");title_font=CreateFontW(-px(27),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");background=CreateSolidBrush(RGB(247,249,252));
 WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=instance;cls.lpszClassName=L"RageDataGenerator";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hbrBackground=background;cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));RegisterClassW(&cls);
 RECT rect{0,0,px(682),px(466)};AdjustWindowRectEx(&rect,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE,0);
 HWND h=CreateWindowExW(0,cls.lpszClassName,L"Generate Data - Rage Racer - PC",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,nullptr);if(!h)return 1;
 if(!request.input.empty()){SetWindowTextW(input,request.input.c_str());}if(!request.output.empty())SetWindowTextW(output,request.output.c_str());
 ShowWindow(h,show);MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){if(!IsDialogMessageW(h,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
 if(worker.joinable()){cancelled=true;worker.join();}DeleteObject(font);DeleteObject(title_font);DeleteObject(background);CoUninitialize();return 0;
}

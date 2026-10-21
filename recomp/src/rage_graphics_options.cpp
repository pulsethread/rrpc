#include "rage_graphics_options.h"
#include "psx_sdl.h"
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <vector>
#define RAGE_WIDE_IMPL(x) L##x
#define RAGE_WIDE(x) RAGE_WIDE_IMPL(x)

namespace {
constexpr int base = 47000;
constexpr int scales[] = {1, 2, 3, 4, 6, 8};
constexpr int rates[] = {-1, 0, 60, 90, 120, 144, 165, 240, -2};
RageGraphicsSettings settings;
std::filesystem::path preferences;
HWND window_handle = nullptr;
WNDPROC previous_proc = nullptr;
HMENU bar = nullptr, graphics = nullptr, resolution = nullptr, aa = nullptr, filtering = nullptr, seams = nullptr, aspect = nullptr;
HMENU motion = nullptr, options = nullptr, anisotropic = nullptr;
HMENU filters = nullptr, help = nullptr, dof_strength = nullptr;
HMENU dof_distance = nullptr;
bool dof_strength_visible = false;
bool anisotropic_visible=false;
constexpr int anisotropies[]={1,2,4,8,16};
std::mutex queue_lock;
std::vector<int> commands;
bool configured = false, dirty = false;

void check(HMENU menu, int first, int count, int selected) {
    for (int i = 0; i < count; ++i)
        CheckMenuItem(menu, first + i, MF_BYCOMMAND | (i == selected ? MF_CHECKED : MF_UNCHECKED));
}
void update_checks() {
    if (!bar) return;
    int scale_index = 0;
    for (int i = 0; i < 6; ++i) if (scales[i] == settings.scale) scale_index = i;
    check(resolution, base, 6, scale_index);
    check(aa, base + 10, 4, settings.aa);
    check(filtering, base + 20, 4, settings.filter);
    const int effects[]={settings.scanlines,settings.crt_texture,settings.crt_shading,settings.crt_distortion,settings.colour_grading};
    for(int i=0;i<5;++i)CheckMenuItem(filters,base+100+i,MF_BYCOMMAND|(effects[i]?MF_CHECKED:MF_UNCHECKED));
    CheckMenuItem(graphics,base+110,MF_BYCOMMAND|(settings.dof?MF_CHECKED:MF_UNCHECKED));
    EnableMenuItem(graphics,base+110,MF_BYCOMMAND|(settings.host_3d?MF_ENABLED:MF_GRAYED));
    if(settings.dof && !dof_strength_visible){
        InsertMenuW(graphics,base+50,MF_BYCOMMAND|MF_POPUP,reinterpret_cast<UINT_PTR>(dof_strength),L"Depth of field strength");dof_strength_visible=true;
        InsertMenuW(graphics,base+50,MF_BYCOMMAND|MF_POPUP,reinterpret_cast<UINT_PTR>(dof_distance),L"Depth of field distance");
    }else if(!settings.dof && dof_strength_visible){
        for(int i=0;i<GetMenuItemCount(graphics);++i)if(GetSubMenu(graphics,i)==dof_strength){RemoveMenu(graphics,i,MF_BYPOSITION);break;}
        for(int i=0;i<GetMenuItemCount(graphics);++i)if(GetSubMenu(graphics,i)==dof_distance){RemoveMenu(graphics,i,MF_BYPOSITION);break;}
        dof_strength_visible=false;
    }
    check(dof_strength,base+120,5,settings.dof_strength-1);
    check(dof_distance,base+130,5,settings.dof_distance);
    if(settings.filter==3 && !anisotropic_visible){
        InsertMenuW(graphics,3,MF_BYPOSITION|MF_POPUP,reinterpret_cast<UINT_PTR>(anisotropic),L"Anisotropic filtering");anisotropic_visible=true;
    }else if(settings.filter!=3 && anisotropic_visible){RemoveMenu(graphics,3,MF_BYPOSITION);anisotropic_visible=false;}
    int aniso_index=0;for(int i=0;i<5;++i)if(settings.anisotropy==anisotropies[i])aniso_index=i;
    check(anisotropic,base+90,5,aniso_index);
    check(seams, base + 30, 3, settings.seams);
    check(aspect, base + 60, 6, settings.aspect);
    for (int i=0; i<9; ++i) if (settings.fps == rates[i]) check(motion, base+70, 9, i);
    EnableMenuItem(graphics, base+40, MF_BYCOMMAND | (settings.host_3d ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(graphics, base+41, MF_BYCOMMAND | (settings.host_3d ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(graphics, anisotropic_visible?4:3, MF_BYPOSITION | (settings.host_3d ? MF_GRAYED : MF_ENABLED));
    CheckMenuItem(graphics, base + 40, MF_BYCOMMAND | (settings.geometry ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(graphics, base + 41, MF_BYCOMMAND | (settings.perspective ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(options, base+80, MF_BYCOMMAND | (settings.original_lod ? MF_UNCHECKED : MF_CHECKED));
    CheckMenuItem(options,base+82,MF_BYCOMMAND|(settings.lod_fade?MF_CHECKED:MF_UNCHECKED));
    CheckMenuItem(options,base+81,MF_BYCOMMAND|(settings.extended_distance?MF_CHECKED:MF_UNCHECKED));
    DrawMenuBar(window_handle);
}
void save() {
    std::error_code error;
    std::filesystem::create_directories(preferences.parent_path(), error);
    auto temporary = preferences;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::trunc);
        out << "# Rage Racer graphics options v1\nscale=" << settings.scale
            << "\naa=" << settings.aa << "\nfilter=" << settings.filter
            << "\ngeometry=" << settings.geometry << "\nperspective=" << settings.perspective
            << "\nseams=" << settings.seams << "\naspect=" << settings.aspect
            << "\nhost_3d=" << settings.host_3d << "\nfps=" << settings.fps
            << "\noriginal_lod=" << settings.original_lod
            << "\nlod_fade=" << settings.lod_fade << "\nextended_distance=" << settings.extended_distance << "\nanisotropy=" << settings.anisotropy
            << "\nscanlines=" << settings.scanlines << "\ncrt_texture=" << settings.crt_texture
            << "\ncrt_shading=" << settings.crt_shading << "\ncrt_distortion=" << settings.crt_distortion
            << "\ncolour_grading=" << settings.colour_grading << "\ndof=" << settings.dof << "\ndof_strength=" << settings.dof_strength << "\ndof_distance=" << settings.dof_distance << '\n';
        out.flush();
        if (!out) { std::fprintf(stderr, "Rage Racer: could not save graphics settings\n"); return; }
    }
    if (!MoveFileExW(temporary.c_str(), preferences.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        std::fprintf(stderr, "Rage Racer: could not replace graphics settings (%lu)\n", GetLastError());
}
LRESULT CALLBACK menu_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if(message==WM_COMMAND && LOWORD(wparam)==base+200){
        rage_graphics_menu_pause(1);
        MessageBoxW(hwnd,L"Rage Racer - PC\nVersion 0.3.21\nBuild: " RAGE_WIDE(__DATE__) L" " RAGE_WIDE(__TIME__)
            L"\n\nOriginal Rage Racer game: Namco\nPC adaptation: Rage Racer PC project\nRecompilation framework: PSXRecomp by Matthew Stanley and contributors\nBIOS: OpenBIOS contributors\n\nLibraries and tools:\nSDL 3, libchdr, libwebp, zlib, zstd, miniz, LZMA SDK\nfmt, toml11, Rabbitizer, ELFIO, stb\nOpenGL and the Windows SDK\nAudio: minimp3 by lieff and contributors (CC0)\nMP3 encoding in Generate Data: LAME (LGPL)\nhttps://lame.sourceforge.io/\n\nLocal source contributions:\nRage Racer PC: retained textures, mesh templates and render submission\n\nResearch references:\nkhasinski: Rage Racer Decomp and Rage Racer PC\n\nThank you to the authors and contributors of these projects.\nOriginal game assets remain the property of their respective owners.",
            L"About Rage Racer - PC",MB_OK|MB_ICONINFORMATION);
        rage_graphics_menu_pause(0);return 0;
    }
    if (message == WM_ENTERMENULOOP) rage_graphics_menu_pause(1);
    if (message == WM_EXITMENULOOP) rage_graphics_menu_pause(0);
    if (message == WM_COMMAND && HIWORD(wparam) == 0 && LOWORD(wparam) >= base && LOWORD(wparam) <= base + 134) {
        std::lock_guard<std::mutex> lock(queue_lock);
        commands.push_back(LOWORD(wparam));
        return 0;
    }
    if (message == WM_NCDESTROY) {
        auto proc = previous_proc;
        window_handle = nullptr;
        previous_proc = nullptr;
        if(!anisotropic_visible && anisotropic)DestroyMenu(anisotropic);
        if(!dof_strength_visible && dof_strength)DestroyMenu(dof_strength);
        if(!dof_strength_visible && dof_distance)DestroyMenu(dof_distance);
        dof_distance=nullptr;
        filters=help=dof_strength=nullptr;dof_strength_visible=false;
        bar = graphics = resolution = aa = filtering = seams = aspect = motion = options = anisotropic = nullptr;anisotropic_visible=false;
        return CallWindowProcW(proc, hwnd, message, wparam, lparam);
    }
    return CallWindowProcW(previous_proc, hwnd, message, wparam, lparam);
}
void item(HMENU menu, int id, const wchar_t* label) { AppendMenuW(menu, MF_STRING, id, label); }
}

void rage_graphics_configure(const char* directory) {
    rage_graphics_read(&settings);
    preferences = std::filesystem::path(directory) / "graphics-options.ini";
    std::ifstream input(preferences);
    std::string line;
    while (std::getline(input, line)) {
        auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        std::string name = line.substr(0, separator);
        std::istringstream value_stream(line.substr(separator + 1));
        int value;
        if (!(value_stream >> value)) continue;
        if (name == "scale") { for (int scale : scales) if (value == scale) settings.scale = value; }
        else if (name == "aa" && value >= 0 && value <= 3) settings.aa = value;
        else if (name == "filter" && value >= 0 && value <= 3) settings.filter = value;
        else if (name == "geometry" && (value == 0 || value == 1)) settings.geometry = value;
        else if (name == "perspective" && (value == 0 || value == 1)) settings.perspective = value;
        else if (name == "seams" && value >= 0 && value <= 2) settings.seams = value;
        else if (name == "aspect" && value >= 0 && value <= 5) settings.aspect = value;
        // Legacy host_3d preferences no longer disable the native renderer.
        else if (name == "anisotropy") {for(int level:anisotropies)if(value==level)settings.anisotropy=value;}
        else if (name == "lod_fade" && (value==0 || value==1))settings.lod_fade=value;
        else if (name == "extended_distance" && (value==0 || value==1))settings.extended_distance=value;
        else if (name == "original_lod" && (value == 0 || value == 1)) settings.original_lod = 0;
        else if (name == "fps") { for (int rate : rates) if (rate == value) settings.fps = value; }
        else if(name=="scanlines" && (value==0 || value==1))settings.scanlines=value;
        else if(name=="crt_texture" && (value==0 || value==1))settings.crt_texture=value;
        else if(name=="crt_shading" && (value==0 || value==1))settings.crt_shading=value;
        else if(name=="crt_distortion" && (value==0 || value==1))settings.crt_distortion=value;
        else if(name=="colour_grading" && (value==0 || value==1))settings.colour_grading=value;
        else if(name=="dof" && (value==0 || value==1))settings.dof=value;
        else if(name=="dof_strength" && value>=1 && value<=5)settings.dof_strength=value;
        else if(name=="dof_distance" && value>=0 && value<=4)settings.dof_distance=value;
    }
    configured = true;
    if ((settings.host_3d || settings.geometry || settings.perspective || settings.seams) && settings.scale < 2) settings.scale = 2;
    if (settings.seams) settings.geometry = 1;
    rage_graphics_apply(&settings);
    dirty = false;
    update_checks();
}
void rage_graphics_install(SDL_Window* window) {
    if (!window) return;
#if defined(PSX_SDL3)
    window_handle = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
#else
    SDL_SysWMinfo info{}; SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info)) window_handle = info.info.win.window;
#endif
    if (!window_handle) return;
    // Apply the executable icon after SDL's archive-provided fallback icon.
    for (const auto size : {ICON_BIG, ICON_SMALL}) {
        const int width = GetSystemMetrics(size == ICON_BIG ? SM_CXICON : SM_CXSMICON);
        const int height = GetSystemMetrics(size == ICON_BIG ? SM_CYICON : SM_CYSMICON);
        const auto icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
            L"IDI_ICON1", IMAGE_ICON, width, height, LR_SHARED));
        if (icon) SendMessageW(window_handle, WM_SETICON, size, reinterpret_cast<LPARAM>(icon));
    }
    bar = CreateMenu(); graphics = CreatePopupMenu(); resolution = CreatePopupMenu();
    aa = CreatePopupMenu(); filtering = CreatePopupMenu(); seams = CreatePopupMenu();
    aspect = CreatePopupMenu();
    motion = CreatePopupMenu();options = CreatePopupMenu();anisotropic=CreatePopupMenu();anisotropic_visible=false;
    filters=CreatePopupMenu();help=CreatePopupMenu();dof_strength=CreatePopupMenu();dof_strength_visible=false;
    dof_distance=CreatePopupMenu();
    const wchar_t* distance_labels[]={L"Near",L"Mid-range",L"Far",L"Very far",L"Horizon"};
    for(int i=0;i<5;++i)item(dof_distance,base+130+i,distance_labels[i]);
    const wchar_t* effect_labels[]={L"Scanlines",L"CRT texture",L"CRT shading",L"CRT distortion",L"Colour grading"};
    for(int i=0;i<5;++i)item(filters,base+100+i,effect_labels[i]);
    item(help,base+200,L"About");
    const wchar_t* strength_labels[]={L"Very subtle",L"Subtle",L"Medium",L"Strong",L"Very strong"};
    for(int i=0;i<5;++i)item(dof_strength,base+120+i,strength_labels[i]);
    const wchar_t* aniso_labels[]={L"1x",L"2x",L"4x",L"8x",L"16x"};
    for(int i=0;i<5;++i)item(anisotropic,base+90+i,aniso_labels[i]);
    // Highest vehicle detail is mandatory.
    item(options,base+81,L"1.5x track draw distance");
    item(options,base+82,L"LOD Fade in");
    const wchar_t* rate_labels[] = {L"Original game cadence", L"Match display", L"60 FPS", L"90 FPS", L"120 FPS", L"144 FPS", L"165 FPS", L"240 FPS", L"Unlimited"};
    for (int i=0; i<9; ++i) item(motion, base+70+i, rate_labels[i]);
    const wchar_t* aspect_labels[] = {L"Original (4:3)", L"Widescreen (16:9)", L"Widescreen (16:10)", L"Ultrawide (21:9)", L"Super ultrawide (32:9)", L"Match window / monitor"};
    for (int i = 0; i < 6; ++i) item(aspect, base + 60 + i, aspect_labels[i]);
    const wchar_t* labels[] = {L"Original (1x)", L"2x", L"3x", L"4x", L"6x", L"8x"};
    for (int i = 0; i < 6; ++i) item(resolution, base + i, labels[i]);
    item(aa, base + 10, L"Off (sharp pixels)"); item(aa, base + 11, L"Smooth scaling");
    item(aa, base + 12, L"FXAA"); item(aa, base + 13, L"FXAA High quality");
    item(filtering, base + 20, L"Original (nearest)"); item(filtering, base + 21, L"Smooth (bilinear)");
    item(filtering, base + 22, L"Smooth 3D, sharp interface");
    item(filtering,base+23,L"Anisotropic filtering");
    item(seams, base + 30, L"Off"); item(seams, base + 31, L"Shared vertices (subpixel)"); item(seams, base + 32, L"Original edge alignment");
    AppendMenuW(graphics, MF_POPUP, reinterpret_cast<UINT_PTR>(resolution), L"3D rendering resolution (restart required)");
    AppendMenuW(graphics, MF_POPUP, reinterpret_cast<UINT_PTR>(aa), L"Antialiasing");
    AppendMenuW(graphics, MF_POPUP, reinterpret_cast<UINT_PTR>(filtering), L"Texture filtering");
    AppendMenuW(graphics, MF_POPUP, reinterpret_cast<UINT_PTR>(seams), L"Reduce polygon gaps (2x or higher)");
    AppendMenuW(graphics, MF_POPUP, reinterpret_cast<UINT_PTR>(aspect), L"Screen aspect ratio");
    AppendMenuW(graphics, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(graphics, MF_POPUP, reinterpret_cast<UINT_PTR>(motion), L"Framerate");
    item(graphics, base + 40, L"Compatibility mode: subpixel correction");
    item(graphics, base + 41, L"Compatibility mode: texture correction");
    AppendMenuW(graphics, MF_SEPARATOR, 0, nullptr);
    item(graphics,base+110,L"Depth of field");
    item(graphics, base + 50, L"Restore original settings");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(graphics), L"&Graphics");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(options), L"&Options");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(filters), L"&Filters");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"&Help");
    SetLastError(0);
    previous_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window_handle, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(menu_proc)));
    if (!previous_proc) { DestroyMenu(bar); bar = nullptr; return; }
    SetMenu(window_handle, bar);
    update_checks();
}
void rage_graphics_tick() {
    if (!configured) return;
    std::vector<int> pending;
    { std::lock_guard<std::mutex> lock(queue_lock); pending.swap(commands); }
    for (int id : pending) {
        if (id >= base && id < base + 6) settings.scale = scales[id - base];
        else if (id >= base + 10 && id < base + 14) settings.aa = id - base - 10;
        else if (id >= base + 20 && id < base + 24) settings.filter = id - base - 20;
        else if (id >= base + 30 && id < base + 33) settings.seams = id - base - 30;
        else if (id == base + 40) { settings.geometry = !settings.geometry; if (!settings.geometry) settings.seams = 0; }
        else if (id == base + 41) settings.perspective = !settings.perspective;
        else if (id == base + 50) { const int original_lod=settings.original_lod, extended_distance=settings.extended_distance, lod_fade=settings.lod_fade;settings = RageGraphicsSettings{}; settings.fps = -1;settings.original_lod=original_lod;settings.extended_distance=extended_distance;settings.lod_fade=lod_fade; }
        else if (id >= base + 60 && id < base + 66) settings.aspect = id - base - 60;
        else if (id == base+82)settings.lod_fade=!settings.lod_fade;
        else if (id == base+81)settings.extended_distance=!settings.extended_distance;
        else if (id >= base+90 && id < base+95)settings.anisotropy=anisotropies[id-base-90];
        else if (id == base + 80) settings.original_lod = 0;
        else if (id >= base + 70 && id < base + 79) settings.fps = rates[id-base-70];
        else if(id==base+100)settings.scanlines=!settings.scanlines;
        else if(id==base+101)settings.crt_texture=!settings.crt_texture;
        else if(id==base+102)settings.crt_shading=!settings.crt_shading;
        else if(id==base+103)settings.crt_distortion=!settings.crt_distortion;
        else if(id==base+104)settings.colour_grading=!settings.colour_grading;
        else if(id==base+110)settings.dof=!settings.dof;
        else if(id>=base+120 && id<=base+124)settings.dof_strength=id-base-119;
        else if(id>=base+130 && id<=base+134)settings.dof_distance=id-base-130;
        else continue;
        if ((settings.host_3d || settings.seams || settings.geometry || settings.perspective) && settings.scale < 2) settings.scale = 2;
        if (settings.seams) settings.geometry = 1;
        dirty = true;
    }
    if (dirty) {
        rage_graphics_apply(&settings);
        if (!pending.empty()) save();
        update_checks();
        dirty = false;
    }
}

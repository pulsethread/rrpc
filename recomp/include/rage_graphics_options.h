#pragma once
struct SDL_Window;
struct RageGraphicsSettings {
    int scale = 1;
    int aa = 1; // 0 sharp, 1 smooth scaling, 2 FXAA, 3 FXAA HQ
    int filter = 0; // 3: anisotropic world filtering, sharp interface
    int anisotropy = 1;
    int extended_distance = 0;
    int lod_fade = 1;
    int geometry = 0;
    int perspective = 0;
    int seams = 0;
    int aspect = 0; // 4:3, 16:9, 16:10, 21:9, 32:9, match window
    int host_3d = 1;
    int original_lod = 0; // Highest car mesh is mandatory.
    int fps = 0; // -2 unlimited, -1 original cadence, 0 display refresh, or a selected target
    int scanlines = 0, crt_texture = 0, crt_shading = 0, crt_distortion = 0;
    int colour_grading = 0, dof = 0, dof_strength = 2; // 1..5
    int dof_distance = 2; // near, mid-range, far, very far, horizon
};
void rage_graphics_read(RageGraphicsSettings* settings);
void rage_graphics_apply(const RageGraphicsSettings* settings);
void rage_graphics_configure(const char* directory);
void rage_graphics_install(SDL_Window* window);
void rage_graphics_tick();
void rage_graphics_menu_pause(int paused);
int rage_graphics_read_aspect();
extern "C" void gl_renderer_set_screen_effects(int texture,int shading,int distortion,int grading,int dof,int strength,int distance);
void rage_graphics_apply_aspect(int mode);
void rage_graphics_apply_motion(int fps);

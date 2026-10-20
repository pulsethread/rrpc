#include "rage_region.h"
#include "cpu_state.h"
#include "rage_track.h"
extern "C" int rage_eu_psx_game_address_in_text(uint32_t);
extern "C" int rage_eu_psx_game_text_native_ok(uint32_t);
extern "C" int rage_eu_psx_game_text_native_ok_full(uint32_t);
extern "C" int rage_eu_psx_dispatch_game_compiled(CPUState*,uint32_t);
extern "C" int rage_eu_psx_game_is_function_entry(uint32_t);
extern "C" int rage_jp_psx_game_address_in_text(uint32_t);
extern "C" int rage_jp_psx_game_text_native_ok(uint32_t);
extern "C" int rage_jp_psx_game_text_native_ok_full(uint32_t);
extern "C" int rage_jp_psx_dispatch_game_compiled(CPUState*,uint32_t);
extern "C" int rage_jp_psx_game_is_function_entry(uint32_t);
extern "C" int rage_us_psx_game_address_in_text(uint32_t);
extern "C" int rage_us_psx_game_text_native_ok(uint32_t);
extern "C" int rage_us_psx_game_text_native_ok_full(uint32_t);
extern "C" int rage_us_psx_dispatch_game_compiled(CPUState*,uint32_t);
extern "C" int rage_us_psx_game_is_function_entry(uint32_t);
extern "C" int psx_game_address_in_text(uint32_t address) {
    if(rage_active_region==1)return rage_jp_psx_game_address_in_text(address);
    if(rage_active_region==2)return rage_us_psx_game_address_in_text(address);
    return rage_eu_psx_game_address_in_text(address);
}
extern "C" int psx_game_text_native_ok(uint32_t address) {
    if(rage_active_region==1)return rage_jp_psx_game_text_native_ok(address);
    if(rage_active_region==2)return rage_us_psx_game_text_native_ok(address);
    return rage_eu_psx_game_text_native_ok(address);
}
extern "C" int psx_game_text_native_ok_full(uint32_t address) {
    if(rage_active_region==1)return rage_jp_psx_game_text_native_ok_full(address);
    if(rage_active_region==2)return rage_us_psx_game_text_native_ok_full(address);
    return rage_eu_psx_game_text_native_ok_full(address);
}
extern "C" int psx_dispatch_game_compiled(CPUState* cpu,uint32_t address) {
    if((address&0x1fffffffu)==(rage_region_address(0x80027ff4u)&0x1fffffffu) &&
       psx_game_text_native_ok_full(address) && rage_track_native_try(cpu,address))return 1;
    if(rage_active_region==1)return rage_jp_psx_dispatch_game_compiled(cpu,address);
    if(rage_active_region==2)return rage_us_psx_dispatch_game_compiled(cpu,address);
    return rage_eu_psx_dispatch_game_compiled(cpu,address);
}
extern "C" int psx_game_is_function_entry(uint32_t address) {
    if(rage_active_region==1)return rage_jp_psx_game_is_function_entry(address);
    if(rage_active_region==2)return rage_us_psx_game_is_function_entry(address);
    return rage_eu_psx_game_is_function_entry(address);
}

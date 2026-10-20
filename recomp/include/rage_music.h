#pragma once
#include <stdint.h>
#ifdef __cplusplus
struct CPUState;
extern "C" {
void rage_music_dispatch(CPUState*,uint32_t);
extern int rage_music_dispatch_pending;
#endif
uint32_t rage_music_custom_origin(void);
uint32_t rage_music_custom_sectors(void);
int rage_music_custom_read(uint32_t,uint8_t*);
void rage_music_track_started(int);
#ifdef __cplusplus
}
#endif

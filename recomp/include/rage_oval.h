#pragma once
#include <stdint.h>
#ifdef __cplusplus
#include "rage_track.h"
#include <filesystem>
// Reads only the verified user's disc. No game assets are compiled into the host.
void rage_oval_prepare(const std::filesystem::path& cue,const std::filesystem::path& executable);
bool rage_oval_extend(const RageTrackCourse& active,RageTrackCourse& drawing);
extern "C" {
#endif
const uint16_t* rage_oval_texture_pixels(void);
const uint16_t* rage_course_texture_pixels(unsigned source);
#ifdef __cplusplus
}
#endif

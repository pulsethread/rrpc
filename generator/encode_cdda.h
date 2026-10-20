#pragma once
#include "generate_data.h"
#include <vector>
// Replaces staged raw audio only after regional data-track verification.
// Returns generated filenames, including the original-sector TOC.
std::vector<std::filesystem::path> encode_cdda(const std::filesystem::path& cue,
    const GenerateProgress& progress,const std::atomic<bool>& cancel);

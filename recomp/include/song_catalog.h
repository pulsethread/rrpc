#pragma once
#include <filesystem>
#include <string>
#include <vector>
struct RageSong {std::string name;int cd_track=0;std::filesystem::path file;};
std::vector<RageSong> rage_song_catalog(const std::filesystem::path& folder);

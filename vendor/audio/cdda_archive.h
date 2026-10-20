#pragma once
#include "minimp3_ex.h"
#include <filesystem>
#include <vector>
#include <memory>
#include <cstdint>

// Versioned, sector-exact TOC alongside generated MP3 tracks. A missing table
// means an original raw disc; an invalid table must fail the mount.
struct CDDAArchive {
    struct Track {
        uint32_t number=0,start=0,pregap=0,count=0;
        std::filesystem::path path;
        mp3dec_ex_t decoder{};
        bool opened=false;
        uint64_t next_frame=0;
        ~Track(){if(opened)mp3dec_ex_close(&decoder);}
    };
    std::vector<std::unique_ptr<Track>> tracks;
    uint32_t leadout=0;
    bool load(const std::filesystem::path& table,uint32_t data_sectors);
    bool contains(uint32_t lba)const;
    bool read(uint32_t lba,uint8_t* bytes);
};

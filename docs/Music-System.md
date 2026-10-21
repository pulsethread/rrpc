# How the music system works

This describes the implementation shipped in v0.3.10. It covers the music
selector shown before a race, the original soundtrack, and the custom MP3
support. For the short instructions on adding a song, see [Music.txt](Music.txt).

The basic idea is to keep the game's existing music playback path and give it
a more flexible source of audio and a larger song catalogue. The original
game still controls when race music starts, when a track ends, and when another
scene needs its own audio. The PC additions handle the catalogue, confirmation,
title display, and conversion between MP3 audio and the CD audio sectors the
game expects.

## From the game disc to the speakers

When Generate Data.exe prepares a new archive, it encodes the disc's audio
tracks as 320 kbps, 44.1 kHz stereo MP3s using LAME. This is a lossy conversion;
the benefit is a smaller archive than storing the complete raw CD audio.
The original data sectors are copied without changing their bytes.

Music on the disc is addressed by track numbers and sector positions. Simply
replacing a BIN file with an MP3 would lose that structure. The generator
therefore writes an accompanying timing table containing the original track
starts, pregap positions, lengths and disc lead-out, together with the MP3
filenames. The runtime can reconstruct the original audio layout from that
information.

Timing matters here. MP3 encoding introduces encoder delay and padding, so
the generator writes a gapless timing tag and then opens each finished file
with the decoder. It checks that the decoded sample count matches the source
sector count exactly. That check protects against a song starting late or a
seek landing at the wrong point, even though the encoded waveform itself is
not identical to the source.

During playback, the game issues its normal CD audio commands. The disc reader
uses minimp3 to supply decoded PCM at the requested sector positions. One CD
audio sector contains 588 stereo sample frames: 1,176 individual 16-bit samples,
or 2,352 bytes. At 75 sectors per second, that gives 44,100 stereo frames per
second.

The CD audio code passes these samples through its existing mute and volume
handling and pushes them into the SPU CD audio path. This means music remains
part of the runtime's existing audio system rather than becoming a separate
desktop media player with its own playback clock. Older archives containing
raw CD audio are still accepted; they do not need to be regenerated just to
use the current executable.

## Building the song catalogue

The catalogue is read from `songs.ini` beside RageRacer.exe. The location is
derived from the executable's path, so it does not depend on whichever working
folder Windows happened to use when launching the game. Custom files live
under the neighbouring `music` folder.

On entering the pre-race selector, the game reloads the catalogue. If the INI
file is missing, it creates one containing the ten original songs. This makes
the file both a working default and an example to edit. Changes made while
the selector is already open are picked up the next time that screen opens.

Entry zero is always RANDOM PLAY. The built-in songs occupy sections
`[song.1]` through `[song.10]`. The first nine map to CD tracks 3 through 11;
DEEP DRIVE, the tenth selection, maps to CD track 17. Other audio tracks serve
other parts of the game, so the song number on screen is not simply the disc
track number.

The built-in mappings are fixed in code. Editing `cd_track=` does not redirect
a built-in entry to an arbitrary disc track. You can rename one with `name=`,
or replace its audio by adding a `file=` value to that section.

Additional sections run from `[song.11]` through `[song.99]`. For example:

```ini
[song.11]
name=AN EVENING DRIVE THROUGH THE CITY
file=Evening Drive.mp3

[song.12]
name=AFTER HOURS
file=Night Routes/After Hours.mp3
```

The file paths are relative to `music`, so the second example refers to
`music/Night Routes/After Hours.mp3`. Spaces, subfolders and quoted values are
supported. Absolute paths, paths with a drive prefix, and path components of
`.` or `..` are rejected. The extension must be `.mp3`, compared without regard
to case.

Entries are read in numeric section order. Invalid or unavailable entries are
left out, so gaps in section numbering do not create empty items in the UI.
The displayed number is the position in the resulting catalogue; it need not
match the section suffix once entries have been skipped.

Custom audio must decode as 44.1 kHz stereo, contain samples, and last no more
than twenty minutes. The catalogue opens each specified MP3 to check those
properties before including it. A missing or invalid replacement file excludes
that entry, including a built-in entry that explicitly requests a replacement.
Removing its `file=` line restores the normal built-in audio on the next load.

## Choosing a song and confirming it

Left and right move through the catalogue and wrap at either end. The PC code
owns this selection while the pre-race screen is active. It consumes those
left/right presses before the original selector can apply them a second time,
then restores the saved pressed-button value when the original update returns.

The screen waits for a fresh press of the accelerator to confirm. It does not
leave just because the old selection timeout has elapsed. This gives the player
time to browse a large list or read an entire scrolling title.

Confirmation follows the game's accelerator assignment. For a digital pad,
the code reads the live accelerator mapping rather than assuming Cross is
always the correct button. For NeGcon layouts, it follows the mapped digital
button or the assigned I/II pressure control. The pressure controls count as
pressed at the implementation's `0x36` threshold.

A small state machine prevents a held accelerator from confirming the screen
as soon as it opens. Confirmation starts unarmed. The player must release the
accelerator before a subsequent press can confirm. Once confirmed, the choice
is held steady while the original transition completes.

The implementation keeps the normal round-screen update running while it
waits. In the original code, the scene timer is incremented before the transition
check at frame 121. Before confirmation, the PC hook caps the incoming timer at
119, so that update reaches 120 without triggering the timed exit. After
confirmation, it allows the timer to reach the transition threshold. Asset
requests and fading still run, and the original code still decides when loading
is ready. Confirmation therefore does not force the race to start before its
assets are available.

## Keeping a larger list compatible with the original game

The new catalogue is stored on the PC side. The original game's shuffle pool,
song-name table and save storage have their original sizes. Writing a custom
catalogue index straight into those tables could make the game read beyond
their bounds.

To avoid that, the hook supplies an original-range value where the game needs
one. During drawing it temporarily uses a valid original name-table index,
including the existing two-digit number layout, and replaces the text arguments
with the actual catalogue number and title. Afterwards it restores the stored
selection value.

RANDOM PLAY retains the original game's shuffle pool. It does not randomly pick
from all the custom files in `music`. A custom song must be selected explicitly
to play it. The expanded catalogue also does not add new song slots to the
original memory-card format.

These hooks use region-specific address mappings for Europe, Japan and USA.
They also check the expected game code and drawing context before replacing
text. The enhanced execution profile enables this selector extension.

## How custom audio becomes race music

When the original round-screen update leaves the selection scene, the PC code
commits the selected song. A built-in song without a replacement keeps its
normal disc track. A custom file uses track 17 as the existing playback route.

For a custom choice, the code opens its own minimp3 decoder and records the
original start position of track 17. The CD audio reader then treats requests
at that position as reads from the custom song. It reports an adjusted audio
extent so the end of the file becomes the end of that playback region.
The ordinary filesystem and game-data reads are not redirected.

Each requested sector is converted to a sample offset by multiplying the
sector index by 1,176. Sequential reads continue through the decoder. If a read
jumps elsewhere, the decoder seeks to the requested sample first. The final
sector is zero-filled before decoding, so a file that ends part-way through
a sector has silence in the unused tail rather than leftover buffer contents.

The original track-17 restart threshold would cut off a custom song longer
than roughly five minutes. While custom playback is active, the code temporarily
extends that threshold and leaves the normal end-of-audio notification to drive
the game's restart behaviour. Returning to the start of the file seeks the
decoder back to sample zero.

Starting a different CD track clears the custom decoder and restores the saved
threshold. Entering another pre-race selection also clears the old custom state.
This keeps a previous race's custom song from replacing unrelated scene audio.

There are two practical limits to keep in mind. First, a file is checked again
when the choice is committed. If it has been moved or become unreadable since
the catalogue opened, the custom override is not installed; the code has already
selected the track-17 route, so the original audio remains the available source.
Second, a mod supplying its own CD audio track layout takes precedence over
this custom-song override in the audio reader.

## Song titles and the twenty-character window

A title may contain up to 160 printable ASCII characters. This matches the
existing font path; it is not Unicode text rendering. An invalid rename of a
built-in song leaves its default title in place. An additional song needs a
valid title to appear.

The visible title is a twenty-character slice of that full string. Earlier
versions reserved two character positions for surrounding quotation marks.
Those added quotation marks are now removed, and v0.3.10 uses their space for
two more title characters. The existing panel therefore displays twenty
characters without being widened.

Only the visible slice is passed to the original text drawing function. The
full name stays in the PC catalogue. The hook reuses the selector's verified
88-byte local text buffer, checks its address and available length, and supplies
a null-terminated string. It does not copy a 160-character title into that
smaller game-side buffer.

## Scrolling, step by step

Titles of twenty characters or fewer stay still. A longer title begins at its
first character and holds there for one second. It then moves left by one whole
character per step, revealing the next character at the right edge. Steps are
150 milliseconds apart. Once the last twenty characters are visible, the title
holds for one second, then moves right in the same whole-character steps until
its beginning is visible again. The cycle repeats.

For a twenty-two-character title, there are just three positions. The window
starts at offset zero. At 1,000 ms it moves to offset one; at 1,150 ms it reaches
offset two, where the end is fully visible. It stays there until 2,150 ms, moves
back to offset one, and reaches offset zero at 2,300 ms. That begins the next
one-second rest at the start. A twenty-one-character title alternates between
its two endpoint positions, each held for one second; it has no intermediate
position to hold for 150 ms.

Changing the selected song resets the clock and returns the new title to its
beginning. The scrolling uses elapsed milliseconds from `GetTickCount64`, not
the scene timer or a count of rendered frames. This keeps the intended timing
independent of PAL/NTSC frame rates and the timer cap used while waiting for
confirmation. If a frame is delayed, the next draw uses the offset appropriate
to the current elapsed time rather than gradually accumulating timing drift.

## Where to look in the source

The catalogue parser and file checks are in
[`song_catalog.cpp`](../recomp/src/song_catalog.cpp). Selection, confirmation,
text replacement and custom decoder ownership are in
[`rage_music.cpp`](../recomp/src/rage_music.cpp). The visible width, scrolling
calculation and confirmation state machine are in
[`rage_music_selector.h`](../recomp/include/rage_music_selector.h).

Disc-audio encoding and timing verification are in
[`encode_cdda.cpp`](../generator/encode_cdda.cpp). The runtime's CD audio bridge
is in [`iso_reader_c.cpp`](../vendor/psxrecomp/runtime/src/iso_reader_c.cpp),
with playback delivery in
[`cdrom.c`](../vendor/psxrecomp/runtime/src/cdrom.c).

[`music_selector_tests.cpp`](../tests/music_selector_tests.cpp) checks the
twenty-character boundary, exact scroll timing, endpoint pauses, confirmation
arming and regional mappings. The existing
[`music_tests.cpp`](../tests/music_tests.cpp) and
[`mp3_tests.cpp`](../tests/mp3_tests.cpp) contain catalogue, decoder, seek,
restart and disc-timing coverage. Those are separate test sources, not a claim
that every audio test is part of the small native-contract test target.

The v0.3.10 live check also used a twenty-character title that stayed still and
a twenty-two-character title that visited offsets zero, one and two. A screenshot
was inspected to confirm that all twenty characters fit the existing panel.

## Implementation walkthrough with source excerpts

The excerpts below are copied from the implementation. They show how native
C++ code cooperates with the recompiled game's registers, guest memory and CD
interfaces. Addresses in the selector hook use the European build as their
canonical form; `address()` maps data locations to the active region, while
`rage_region_code()` normalizes mapped code entry points to canonical physical
addresses. They are guest addresses, not pointers to host process memory.
### Dispatch entry and return tracking

The scene dispatcher enables music callbacks at the round-screen entry and while a callback return is pending. The music hook first filters targets and validates the enhanced profile and memory callbacks. It records the guest return register (`gpr[31]`, MIPS RA) and stack register (`gpr[29]`, SP). Matching both target and stack lets it restore temporary values after the intercepted call. When the scene ID leaves 10, it commits the selection and disables pending dispatch. This keeps the full music hook out of ordinary race dispatch once selection is complete.

```cpp
    // Music callbacks run only on the short pre-race screen; avoid another
    // function call at every guest dispatch throughout a live race.
    if(target==0x1cfb4 || rage_music_dispatch_pending)rage_music_dispatch(cpu,target);

extern "C" void rage_music_dispatch(CPUState* cpu,uint32_t raw_target){
    const uint32_t target=raw_target<0x80000000u?raw_target:rage_region_code(raw_target);
    if(target!=0x1cfb4 && target!=0x1cd54 && target!=0x16754 && target!=round_return && target!=draw_return)return;
    if(!PSX_EXECUTION_ENHANCED || !cpu || !cpu->read_word || !cpu->write_word || !cpu->read_half || !cpu->write_half || !cpu->write_byte)return;
    const uint32_t sp=cpu->gpr[29],entry=address(0x80000000u|target);
    if(draw_return && target==draw_return && sp==draw_sp){
        cpu->write_word(address(0x801e42cc),saved_selection);draw_return=0;
    }
    if(round_return && target==round_return && sp==round_sp){
        cpu->write_half(address(0x801e436e),saved_buttons);round_return=0;
        if(cpu->read_word(address(0x801e42e4))!=10){commit(cpu);in_round=false;rage_music_dispatch_pending=0;}
    }
```

### Mapped controls and a fresh confirmation press

The controller type and live accelerator mapping come from guest memory. NeGcon pressure inputs use the 0x36 threshold; other mappings use a digital bit mask. The confirmation object below arms on release and confirms on a later rising edge. Once confirmed it stays latched until the object is reset on selector entry.

```cpp
bool accelerator_down(CPUState* cpu){
    const uint32_t pad=address(0x801e436e)-6;
    const bool negcon=(cpu->read_half(pad)>>8)==0x23;
    if(negcon){
        const unsigned mapping=cpu->read_half(address(0x8019cb08));
        if(mapping==0 || mapping==5)return cpu->read_half(pad+12)>=0x36;
        if(mapping==1 || mapping==3 || mapping==6)return cpu->read_half(pad+14)>=0x36;
    }
    return (cpu->read_half(pad+2)&cpu->read_half(address(0x801e4b60)+(negcon?10:2)*2))!=0;
}

struct RageMusicConfirmation {
    bool armed=false,previous=false,confirmed=false;
    bool update(bool down){
        if(!down)armed=true;
        if(armed && down && !previous)confirmed=true;
        previous=down;
        return confirmed;
    }
};
```

### Keeping loading alive while waiting

The original update increments its timer before checking frame 121. Capping the incoming value at 119 keeps that update at 120 while the player browses. Confirmation raises a smaller value to 120 so the original transition can proceed. Clearing bits 0xa000 consumes left/right for this call; the return hook restores the original pressed-button word. The separate native selection may exceed the original catalogue, so the guest selection is reduced to a valid built-in index before the original update accesses its tables.

```cpp
        const bool confirmed=confirmation.update(accelerator_down(cpu));
        const uint32_t timer=cpu->read_word(address(0x801e40b8));
        // UpdateRoundScreen increments this timer before its original 121-frame
        // transition. Loading and fading continue normally while we wait.
        if(!confirmed && timer>119)cpu->write_word(address(0x801e40b8),119);
        else if(confirmed && timer<120)cpu->write_word(address(0x801e40b8),120);
        cpu->write_half(address(0x801e436e),saved_buttons&~0xa000);
        // Keep original shuffle/count/save storage within its original bounds.
        const uint32_t base=selected<songs.size() && songs[selected].file.empty() && songs[selected].cd_track>=3 && songs[selected].cd_track<=11?uint32_t(songs[selected].cd_track-2):0;
        const uint32_t count=cpu->read_word(address(0x801e40a8));
        cpu->write_word(address(0x801e42cc),base<=count?base:0);
```

### Drawing through a bounded guest buffer

The drawing hook temporarily limits the guest selection to index 10, which retains the original two-digit layout. It intercepts only the expected text function, stack depth and drawing coordinates. The title is sliced to twenty characters before text_argument copies it into the existing 88-byte stack buffer. The checks require the cached guest RAM segment, enough space within 2 MiB of physical RAM, and room for a terminator. Register gpr[6] is the third MIPS argument (a2); redirecting it changes the string consumed by the original renderer. The return hook restores the saved selection.

```cpp
void text_argument(CPUState* cpu,const std::string& text){
    // DrawBgmSelector owns an88-byte local text buffer at SP+40. Reuse that
    // buffer only during its verified text calls; no spare guest RAM arena.
    const uint32_t p=cpu->gpr[29]+40,physical=p&0x1fffffffu;
    if((p>>24)!=0x80 || physical>0x200000-88 || text.size()>=88)return;
    for(size_t i=0;i<=text.size();++i)cpu->write_byte(p+uint32_t(i),i<text.size()?uint8_t(text[i]):0);
    cpu->gpr[6]=p;
}

    if(target==0x1cd54 && in_round && !songs.empty() && cpu->read_word(entry)==0x27bdff60 && psx_game_text_native_ok_full(entry)){
        saved_selection=cpu->read_word(address(0x801e42cc));
        // Value10 selects the original two-digit arrow/number layout. Its
        // name-table index is valid, even for an independently numbered song.
        cpu->write_word(address(0x801e42cc),std::min(selected,10u));
        draw_return=rage_region_code(cpu->gpr[31]);draw_sp=sp;return;
    }
    if(target==0x16754 && draw_return && sp==draw_sp-160 && cpu->gpr[5]==0xce){
        if(cpu->gpr[4]==0x90)text_argument(cpu,songs[selected].name.substr(rage_music_title_offset(songs[selected].name.size(),GetTickCount64()-title_started),rage_music_title_width));
        else if(cpu->gpr[4]==0x74 || cpu->gpr[4]==0x78)text_argument(cpu,std::to_string(selected));
    }
```

### Elapsed-time scrolling arithmetic

For a title of length L, distance = L - 20 is the final window offset. There are distance - 1 intermediate offsets on each leg, each lasting 150 ms. The period therefore contains two 1,000 ms endpoint holds plus two intermediate journeys. The modulo calculation selects the current phase directly from elapsed time. The early return prevents unsigned underflow for short strings; distance = 1 gives travel = 0 and only the endpoint holds. The caller supplies GetTickCount64() - title_started and resets title_started when selection changes.

```cpp
// Each endpoint rests for one second; intermediate characters last 150 ms.
inline constexpr size_t rage_music_title_width=20;
inline size_t rage_music_title_offset(size_t length,uint64_t elapsed){
    if(length<=rage_music_title_width)return 0;
    const size_t distance=length-rage_music_title_width;
    const uint64_t travel=(distance-1)*150;
    const uint64_t phase=elapsed%(2000+2*travel);
    if(phase<1000)return 0;
    if(phase<1000+travel)return 1+size_t((phase-1000)/150);
    if(phase<2000+travel)return distance;
    return distance-1-size_t((phase-2000-travel)/150);
}
```

### Opening a custom decoder and retaining the CD route

The selected CD number is translated to the guest playback value by subtracting three. A custom file then opens a sample-addressable decoder and borrows track 17. decode_location converts the guest BCD minute/second/frame location into an LBA, subtracting the standard 150-sector offset. Decoder sample counts include both stereo channels, so rounding up by 1,176 produces a sector count. Format, duration, origin and disc extent checks run before installation. The saved loop threshold is replaced while the override is active; clear_custom restores it and destroys the decoder through its RAII owner.

```cpp
uint32_t decode_location(uint32_t loc){
    auto bcd=[](uint32_t b){return (b>>4)*10+(b&15);};
    const uint32_t msf=(bcd(loc&255)*60+bcd((loc>>8)&255))*75+bcd((loc>>16)&255);
    return msf>=150?msf-150:0;
}
void commit(CPUState* cpu){
    clear_custom();if(!selected || selected>=songs.size())return;
    const auto& song=songs[selected];cpu->write_word(address(0x801e40e0),uint32_t(song.cd_track-3));
    if(song.file.empty())return;
    auto active=std::make_unique<Custom>();
    if(mp3dec_ex_open_w(&active->decoder,song.file.c_str(),MP3D_SEEK_TO_SAMPLE)!=0)return;
    active->opened=true;active->origin=decode_location(cpu->read_word(address(0x8009afd4)+17*4));
    if(active->decoder.info.hz!=44100 || active->decoder.info.channels!=2 || !active->decoder.samples ||
       active->decoder.samples>uint64_t(44100)*2*60*20 || !active->origin)return;
    active->sectors=uint32_t((active->decoder.samples+1175)/1176);
    if(active->origin+active->sectors>449999)return;
    // The original five-minute restart threshold must not truncate a longer
    // custom song. Hardware EOF still drives the normal track restart.
    old_loop=cpu->read_word(address(0x8007f5b0)+17*4);loop_saved=true;
    cpu->write_word(address(0x8007f5b0)+17*4,0x00005999);
    std::fprintf(stderr,"Music: custom selection %u, %u sectors\n",selected,active->sectors);
    custom=std::move(active);
}
```

### Reading one sector from the custom MP3

One sector is 588 stereo frames, or 1,176 interleaved signed 16-bit samples and 2,352 bytes. The decoder API seeks in individual samples, hence sector * 1176. The next cursor avoids redundant seeks for sequential reads. A jump back to zero is counted as a restart. The buffer is cleared before reading the final partial sector; only the remaining samples are requested. A failed seek or short read returns failure to the CD bridge.

```cpp
extern "C" int rage_music_custom_read(uint32_t sector,uint8_t* bytes){
    if(!custom || !bytes || sector>=custom->sectors)return 0;
    const uint64_t sample=uint64_t(sector)*1176;
    if(!sample && custom->next)++custom_restarts;
    if(custom->next!=sample && mp3dec_ex_seek(&custom->decoder,sample)!=0)return 0;
    const size_t want=size_t(std::min<uint64_t>(1176,custom->decoder.samples-sample));
    std::memset(bytes,0,2352);
    if(mp3dec_ex_read(&custom->decoder,reinterpret_cast<mp3d_sample_t*>(bytes),want)!=want)return 0;
    custom->next=sample+want;++custom_reads;return 1;
}
```

### Connecting the override to the CD reader

The bridge exposes the custom end position as origin + sectors and subtracts origin before requesting a custom sector. Its bounds check is completed by rage_music_custom_read. A mod-provided CD layout takes precedence. These functions serve CD audio reads; ordinary game-data reads keep their existing path. The override is cleared when a different CD track starts, so scene audio can return to the disc archive.

```cpp
uint32_t iso_cdda_sector_count(void* handle) {
#ifdef RAGE_GRAPHICS_OPTIONS
    if(!mod_runtime_cdda_track_count() && rage_music_custom_sectors())
        return rage_music_custom_origin()+rage_music_custom_sectors();
#endif
    return mod_runtime_cdda_track_count()?mod_runtime_cdda_track_start(0):iso_sector_count(handle);
}
int iso_read_cdda_sector(void* handle,uint32_t lba,uint8_t* buffer,int size) {
    if(!handle || !buffer || size<2352) return 0;
#ifdef RAGE_GRAPHICS_OPTIONS
    if(!mod_runtime_cdda_track_count() && rage_music_custom_sectors() && lba>=rage_music_custom_origin())
        return rage_music_custom_read(lba-rage_music_custom_origin(),buffer);
#endif
    if(!mod_runtime_cdda_track_count()) return iso_read_raw_sector(handle,lba,buffer,size);
    uint32_t source=0;
    const int kind=mod_runtime_read_cdda_sector(lba,buffer,size,&source);
    return kind==1 || (kind==2 && static_cast<PS1::ISOReader*>(handle)->ReadRawSector(source,buffer));
}
```

### Preserving original disc timing in compressed archives

The archive decoder measures its cursor in stereo frames, then multiplies by two for the minimp3 sample API. Its track range begins at the stored pregap, preserving the original sector layout. During load it requires decoder.samples == track.count * 1176, stereo and 44.1 kHz. The generator configures LAME for 320 kbps CBR, writes the LAME timing frame after flushing, then reopens the file to verify this exact decoded count. This checks duration after encoder-delay and padding removal, rather than assuming a compressed byte length determines playback timing.

```cpp
bool CDDAArchive::read(uint32_t lba,uint8_t* bytes){
    for(auto& t:tracks)if(lba>=t->pregap && lba-t->pregap<t->count){
        const uint64_t frame=uint64_t(lba-t->pregap)*588;
        if(t->next_frame!=frame && mp3dec_ex_seek(&t->decoder,frame*2)!=0)return false;
        if(mp3dec_ex_read(&t->decoder,reinterpret_cast<mp3d_sample_t*>(bytes),1176)!=1176)return false;
        t->next_frame=frame+588;return true;
    }
    return false;
}
```

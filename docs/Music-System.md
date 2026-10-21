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

# Rage Racer - PC

This folder contains the Windows source files and build inputs for both
`RageRacer.exe` and `Generate Data.exe`. It is independent of `Port`, `Source`,
`Reversing` and the old attempt. No original disc image, personal saves,
`data.dat`, compiled game executable or development cache is included.
The included open-source OpenBIOS firmware is a required build input. No
original disc bytes, regional conversion patches or complete images are included.
Generated game C is included so building does not require a disc, Ghidra or
running the recompiler again. Framework code is the pinned, locally adapted
psxrecomp runtime; dependency notices are retained with their sources.

## Build

Use Windows x64, recent Visual Studio C++ Build Tools (including the Windows
SDK and MSVC C11 atomics support), CMake 3.24+ and Python 3.10+. The first
configuration downloads the pinned SDL3, libchdr, zlib and libwebp dependencies.
An internet connection is required for that first configuration. CMake verifies
the dependency archives against the supplied manifest.

From this folder:

```powershell
./Build.ps1
```

Or build outside the source folder:

```powershell
cmake -S . -B ../Build-PC -DPSX_DEBUG_TOOLS=ON
cmake --build ../Build-PC --config Release --target rage-racer generate-data --parallel 2
```

Build.ps1 keeps generated files outside Src, in ../Build-PC. Both executables
are placed in the build folder's `bin/Release` directory.
Distribute the two executables together. `Generate Data.exe` embeds open runtime
support and identification metadata only; it does not need Python installed on the
player's computer. Neither executable needs the source tree after building.

## Create game data

Run `Generate Data.exe`, choose a CUE or ISO image, or select the physical CD
in an optical drive, then choose Generate data. Supported source revisions are:

- Europe: SCES-00650.
- USA: SLUS-00403.
- Japan v1.1: SLPS-00600.

Each region retains its original executable, assets, text, video and audio.
RageRacer.exe selects the matching translated routines and scene address profile;
no disc is converted to another region. The generator verifies the selected
revision's executable and data track, and copies the original tracks unchanged.
Unknown or modified revisions fail without replacing an existing archive.
Raw/cooked ISO and split/combined BIN/CUE inputs are supported. Combined-image
audio indices are retained unchanged. Translated game routines are included
in RageRacer.exe; original disc payloads and conversion patches are not included
in either executable. All original game content in data.dat comes from the
user's selected disc. No extra regional disc is required.
CUE/BIN images and physical CDs retain the available CD music. Data-only ISO
images have no separate CD music and may lack XA video/audio sectors; those
missing bytes cannot be recovered by this tool. Use a full disc for the full
soundtrack and intro. Physical reading uses Windows raw CD reads with the
original TOC, without substituting unreadable sectors.

The tool copies sources into private temporary storage, verifies the game's
executable and regional track checksum, hashes
every archive entry and rereads the result before publishing
`data.dat`. Existing archives are replaced only after successful completion;
cancelling or failing leaves them untouched. Temporary storage is removed on
normal completion or cancellation. Allow roughly twice the disc size in free
space while generating. Reading a physical CD depends on the drive supporting
raw reads; no optical drive was available for hardware verification during
this implementation.

Keep `data.dat`, `RageRacer.exe` and `Generate Data.exe` together. If the archive
is missing, the game offers to open the generator or close. The generator can
launch the game after successfully creating data in the same folder.

The developer-only command-line entry point is:

```powershell
& './Generate Data.exe' --headless --input 'D:/Disc/game.cue' --output 'D:/Game'
& './Generate Data.exe' --headless --drive 'E:' --output 'D:/Game'
```

An existing archive requires `--replace`; failures return a nonzero exit code.
Track and vehicle rendering always use the highest available source detail.
Native terrain preserves the original and optional 1.5x draw distance.


Both application icons are built from `assets/Game.ico`, copied from the project root `Game.ico`. Keep this asset with the sources when rebuilding. The game also applies the embedded icon to its window after loading the archive.

Archives made by the retired regional-conversion generator remain compatible as
European data. To retain your USA or Japan disc’s own region, regenerate data.dat
with this generator. Existing data.dat is never changed by updating the programs.


Generate Data now encodes original CD audio to 320kbps,44.1kHz stereo MP3
using LAME3.100. Original data sectors and CD timing remain unchanged. The game
decodes with minimp3 and still accepts older raw-audio archives. Licensing and
encoder relink materials are provided under Shareable/Licenses.

Custom songs are configured in songs.ini beside RageRacer.exe. Add [song.11]
with name=MY SONG and file=MySong.mp3, placing the MP3 inside music/. Select it
with left/right on pre-race MUSIC SELECT. Use 44.1kHz stereo MP3, up to 20 minutes.
Changes are read at the next pre-race screen. Titles support up to 160 ASCII
characters and scroll through an 18-character window, pausing for one second
at either end and advancing one character every 0.3 seconds. Press your mapped
accelerate button to confirm the song and continue. Random Play retains the original built-in shuffle
pool. The independent selector preserves original shuffle/save table sizes.
See docs/Music.txt for the exported original song/CD-number mappings.

See RELEASE.md for the current release and minimal portable folder contents.

Download the two Windows executables from [release/](release/), packaged as
`RageRacer-PC-v0.3.8.zip`. The setup instructions are in [release/README.md](release/README.md).

A decent portion of this codebase was developed using AI, so expect bugs!

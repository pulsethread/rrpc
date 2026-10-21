# Windows release

Download and extract `RageRacer-PC-v0.3.8.zip` into a writable folder. It contains
`RageRacer.exe` and `Generate Data.exe`.

1. Run `Generate Data.exe` and select your game disc, CUE or ISO image.
2. Generate `data.dat` beside the two executables.
3. Run `RageRacer.exe`.

Supported game revisions: Europe SCES-00650, Japan SLPS-00600 and USA SLUS-00403.
No source checkout or development tools are required to play. The game creates
its runtime files and settings as needed. Keep `data.dat`, your saves, settings
and custom music when updating the executables. Existing compatible `data.dat`
files can be reused.

Version 0.3.8 adds scrolling long song titles and accelerator-button confirmation
to music selection, and includes the recent rendering and wheel/shadow fixes.

Dependency notices and codec source/relinking materials are retained in the
repository's `vendor/` directory; see `THIRD_PARTY.md` and `RELEASE.md` at the
repository root.

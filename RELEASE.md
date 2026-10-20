# Release 0.3.3

This source includes the native terrain rendering improvements, mirror and replay
terrain opacity fix, full-width replays, MP3 disc audio, and configurable music
selection. Build the game and generator with `Build.ps1`.

The current verified game executable has SHA-256:
`9d97fbed811850ede93726248d42fc892d93a4e5bb15737232e2032d16690706`.
The unchanged audio generator has SHA-256:
`ec77e654aa784a6dd306849158135b227e56d78341632dd4c6219b4cd207096a`.
Rebuilding can change executable hashes because build timestamps are embedded.

## Minimal portable folder

- `RageRacer.exe`, `Generate Data.exe`, and `data.dat`.
- `Licenses/`, including the MP3 encoder source and relinking materials.
- Existing `saves/`, `input.ini`, and `keybinds.ini` to preserve user progress and settings.
- `songs.ini` and `music/` to preserve the music catalogue and custom songs.
- Any installed user mods, if present.

The game regenerates `.runtime/` from the verified archive when required.
Runtime and overlay caches are generated files, not release inputs. Overlay
captures are debugging output. Remove these only while the game is closed.
Generated files can reappear after playing.

The music guide is in [docs/Music.txt](docs/Music.txt). Rendering implementation
and validation details are in [RENDERING_NOTES.md](RENDERING_NOTES.md).

The 20 October cleanup moved old caches, debugging captures, the default empty
mod state, and the standalone music guide outside Shareable. All retained files,
including custom music, were checked against their pre-cleanup SHA-256 hashes.

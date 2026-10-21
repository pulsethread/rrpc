# Release 0.3.8

Music selection now waits for a fresh press of the mapped accelerator button.
Long titles scroll through an 18-character window, resting at either end for
one second and advancing one character every 0.3 seconds. Changing songs resets
the scroll. Digital and NeGcon accelerator assignments are respected.

Validation: all 13 native contract tests passed, including exact scroll timing
boundaries. A live PAL selector test verified scrolling in both directions,
selection reset, waiting beyond the old timeout, ignoring accelerate held on
entry, and confirming with a remapped accelerator before entering the race.

The 0.3.7 follow-up stabilizes AI wheel geometry and cast shadows by retaining
opaque wheel faces through the original integer-screen winding test and using
a stable camera anchor when the player's body is off-screen. Player wheel/blur
banks are also kept separate from legacy shadow-plate identification.

This release adapts reference car rendering to the native renderer: body and
wheel motion is interpolated in world space, the assembled car respects the
reference ground constraint, and body/wheel geometry casts directional shadows
onto terrain and scenery. Existing car paint, steering, banked suspension,
wheel animation and GTE lighting remain. Vehicle self-shadowing, the reference
PBR/reflection shaders and ray tracing are not included.

Previous terrain performance improvements, starter clipping fixes, replay-only
PS1 draw distance, full-width rendering and MP3/custom music remain included.
Build the game and generator with `Build.ps1`.

The current verified game executable has SHA-256:
`64a90c28ef826758a82c48928c5528328e9f863495948e422fa90af33a155cdf`.
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

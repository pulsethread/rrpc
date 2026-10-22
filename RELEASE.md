# Release 0.3.22

Add a live minimap at the middle left of the racing view. The complete track
centreline is white, the player is a blue directional arrow, and active AI cars
are red dots. Car positions update each race tick, including off-screen AI.
The transparent map scales with the displayed game area and stays separate
from mirror views, with outlined markers for contrast on bright roads.

Validation: all 15 native tests passed, including minimap fitting and cardinal
headings. Live race captures verified placement at 4:3, 16:9 and 32:9; all 162
graphics checks passed. Original game data, saves and settings are unchanged.

# Release 0.3.21

This release combines the rendering improvements since v0.3.10: cached GPU mesh
layouts and palettes, camera-inverse reuse, caster-only shadow geometry, improved
terrain lighting and shadow filtering, and reference-derived car lamps.

Headlights activate only in tunnels and aim downward with an upper cutoff.
Rear lamps activate in tunnels or while braking. Active lamps illuminate nearby
surfaces. Subtle rear trails follow fast screen movement in tunnels, fade over
0.18 seconds and are hidden by obstructing geometry. Headlights leave no trails.
The fallback background behind track gaps is now very dark grey.

AI trail history follows animation time rather than render submission order,
preventing future real frames from leaking into older interpolated frames.
Alternating framebuffer pages share one camera-relative trail history.

Validation: all 14 native tests passed, and all 162 graphics checks passed in
4:3, 16:9 and 32:9. Isolated game smoke tests and bundle verification passed.
The data generator, original game data, saves and settings are unchanged.

# Release 0.3.20

The flat fallback background behind the track is now very dark grey (#0c0c0c),
making gaps in world geometry less conspicuous. Sky gradients and cloud textures
retain their authored colours.

Rear trails use consecutive lamp screen positions and connect to the current
lamp, producing a continuous fading streak rather than sparse deposits. The
screen-speed threshold controls brightness without discarding the path.

Scene-depth visibility queries now gate each lamp before recording or drawing
its effect. Obstructed sources clear their history, preventing trails through
geometry or across hidden intervals. Ribbons also obey scene depth. Fix an
undefined lamp-shader clip distance that could cause intermittent clipping.
Headlight beam settings and rear-trail intensity remain unchanged.

# Release 0.3.19

Rear trails behave as brief camera-lens stains: deposited positions stay fixed
and fade independently of the lamp or passing geometry. Filtered screen motion
must exceed a threshold before depositing a trail; small and slow movements
produce none. A lower stopping threshold prevents flicker around activation.
Headlights and rear-trail intensity are unchanged.

# Release 0.3.18

Rear-light trails are approximately half as intense, retaining their smooth
joined shape and fade. Headlights now aim downward so road illumination begins
closer to the car, including the player's cockpit lighting. The upper cutoff
and existing lamp activation rules remain unchanged.

# Release 0.3.17

Rear-light trails now form a connected ribbon with shared joins and a live
endpoint on every frame. The oldest end fades smoothly to zero instead of
dropping whole segments. Consistent current-frame depth avoids flicker caused
by comparing old trail depths against the moving car. Headlight settings and
the tunnel-only trail activation remain unchanged.

# Release 0.3.16

Rear-light trails now follow movement across each camera view, including camera
motion and low-speed cars. They are brighter, wider and fade over 0.18 seconds.
They remain tunnel-only, and headlights do not leave trails.

Headlight surface intensity is reduced to 35% of the original level. An upper
cutoff prevents illumination above headlight height, with a soft edge below
the cutoff. Rear-light spill and existing lamp activation rules are unchanged.

# Release 0.3.15

Remove headlight motion trails while preserving red rear-light trails in tunnels.
Reduce headlight surface-light intensity by 15% without dimming the car lenses
or changing rear lamps. Existing tunnel and braking activation rules remain.

# Release 0.3.14

Active car lamps once again illuminate nearby road surfaces. Headlights remain
tunnel-only; rear lamps remain limited to tunnels or actual braking. Fast cars
now produce both white headlight trails and red rear-light trails in tunnels.
Trail brightness no longer depends on facing the lamp, so motion trails can
be seen when following a car. Scene depth still hides trails behind solid surfaces.

# Release 0.3.13

Headlights illuminate only in tunnels. Rear lamps illuminate only in tunnels
or while the car is braking; the daylight tail-light baseline and brake-release
fade have been removed. Lamps switch off on leaving tunnel lighting unless
the car is braking. Fast cars retain their brief rear-light trails in tunnels.

Added lighting is restricted to car lenses, their glows and tunnel trails.
Car lamps no longer cast additional light onto roads, scenery or tunnel fixtures.

# Release 0.3.12

Cars now use the reference project's per-model lamp locations and lens masks.
Rear lights remain dimly illuminated in daylight and brighten in tunnels;
brake input produces brighter stop lamps with a short release fade. Headlights
turn on after sustained tunnel darkness, illuminate nearby surfaces and work
in cockpit views where the original game does not draw the player body.

At speeds above 80 km/h in tunnel lighting, rear lights produce a short world-
space trail. Its intensity increases up to 160 km/h and its history lasts at
most 0.12 seconds. Trails follow individual lamps, fade in elapsed time, obey
opaque scene depth, and reset after camera gaps, teleports and state loads.
The original textures, car paint, physics and controls remain in use.
# Release 0.3.11

Native meshes now retain their GPU vertex layout, and queued scene groups reuse
palette storage. Camera inverses are resolved once per distinct viewport rather
than searching and inverting again for every group. Mixed meshes keep a separate
cached caster index list so non-casting terrain is omitted from the light pass.

Terrain lighting now combines the reference stage's ambient and directional
components with the existing authored colours. Surface orientation modulates
illumination, and shadow visibility attenuates direct light more strongly than
ambient light. Car paint retains its existing authored lighting. The stable
reference sun direction and snapped 4096-square map remain in use.

Shadow surface derivatives are evaluated before alpha/coverage discard; slope
bias is consistent through reflected views, and PCF uses the actual depth texture
size. The driver probe includes six new ambient/diffuse and mirror cases.
# Release 0.3.10

The music selector now displays 20 title characters, using the two spaces
freed by removing quotation marks. Titles of up to 20 characters stay static;
longer names retain 0.15-second scrolling steps and one-second endpoint pauses.

Song titles display without surrounding quotation marks and scroll one letter
every 0.15 seconds, retaining the one-second pauses at either end. The PAL boot
notice is skipped while preserving the following display setup, Namco logo,
asset loading and intro. Regions without that notice keep their normal boot.

Music selection now waits for a fresh press of the mapped accelerator button.
Long titles scroll through an 20-character window, resting at either end for
one second and advancing one character every 0.15 seconds. Changing songs resets
the scroll. Digital and NeGcon accelerator assignments are respected.

Validation: all 13 native contract tests passed, including exact scroll timing
boundaries and boot-notice scope. Live EU/US/JP boots reached the intro; the
PAL notice was skipped. A live PAL selector test verified scrolling in both directions,
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
`fcb8d044aa0d6dadc971175e9e9cf967a6ff51e617631a81621559b5670ebbf1`.
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

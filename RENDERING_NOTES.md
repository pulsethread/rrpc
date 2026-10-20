# Native replay and texture-fragment fixes

Native scene replay verifies and reuses retained meshes before computing a
content hash, and avoids redundant projected-vertex copies and CRT clamps.
Exact byte comparisons remain authoritative even when a replay hint is stale.
The fallback hash processes four independent word lanes instead of multiplying
once for every byte. The retained mesh cache keeps its existing size.

Connected irregular texture fragments share filtering bounds only across a
complete edge with matching positions, UVs, material and pose. Rectangular atlas
tiles, UV discontinuities, separate instances, repeated corners and course
sheets retain independent bounds. Filtering bounds are prepared on a mesh-cache
miss, while the exact original input remains the cache identity. Positions,
UVs, colours and depth behaviour are unchanged.

This addresses filtering seams through fan-shaped surfaces such as the glass
bridge's centre. The captured fan triangles shared their boundary positions
and did not geometrically overlap; applying a depth offset would not address
the different texture-clamping regions.

Standalone regression checks and build instructions are in `tests/`.

Course submission caches each shared vertex's camera projection once per view
and draw. Triangles wholly outside one viewport half-space are rejected before
building renderer receipts or changing texture-window state. The existing
viewport guard band remains, and rejection with camera history requires the
same outside plane at both endpoints. Straddling triangles survive, including
near-plane crossings. Far distance remains governed by the existing sector
policy. This reduces unnecessary geometry and texture preparation around the
last tunnel without changing the selected draw distance.

The host instruction-pointer sampler is opt-in with `PSX_HOST_PROFILE=1`.
On Windows it suspends the emulation thread every millisecond; enabling it
for ordinary play or counter-only frame timing introduces measurement overhead.
The debug server and lightweight frame counters remain available without it.

## Vehicle shells and optional screen effects (0.2.0)

Modern vehicle capture retains both windings of opaque, dark, flat tyre-shell
faces in body entries. The game otherwise discards faces at the real camera
before the intermediate camera can see them. The scope excludes wheel-face
entries, shadow entries, textured surfaces, coloured/bright faces and blended
commands. The original wheel-face animation and native opaque depth writes
remain. Region mappings cover the European, Japanese and US flat handlers.
The GTE winding override runs only during the proven vehicle draw, outside
speculative/replay sandboxes; the execution compatibility fingerprint changes.

Filters are independent, persistent toggles: scanlines, RGB phosphor texture,
CRT corner shading, barrel distortion, and a mild warm colour grade. They run
on the composed game rectangle on both real and generated frames. Distortion
leaves the application menus and letterbox region outside the effect.

Depth of field is optional and requires Modern 3D rendering. It resolves native
scene depth before interface batches, preserving HUD detail and the mirror's
independent depth domain. The near road/vehicles remain in focus; distant
surfaces soften with five strengths. Depth-aware taps reduce foreground colour
bleed. Scratch surfaces are retained separately per view and target, avoiding
main/mirror reallocations every frame. Real and generated view bookkeeping is
saved/restored independently. All new effects default off and allocate no
effect surfaces until used. Settings remain in graphics-options.ini.

Help > About displays the PC application version, compile date/time and project
and dependency credits. Driver and manual validation records are under
Port/data/FILTERS_TYRES_VALIDATION.md in the development workspace.

## Vehicle depth and widescreen follow-up (0.2.1)

Retain opaque flat body panels (including black front tyre strips) and the two
opaque GT4 body streams through guest winding rejection. Native depth testing
then makes the body occlude wheel caps at intermediate camera poses. Shadow
and rotating-wheel entries remain excluded. GT4 handler mappings are verified
against the EU, JP and US generated declarations. This does not change the
execution fingerprint or relax savestate compatibility checks.

Vehicle shadow entry 1 now carries RAGE_SCENE_DECAL into immutable scene
receipts. It uses the existing polygon offset and no-depth-write road-decal
pass instead of competing with road triangles as an opaque depth writer.

Racing screen-space packets use shared centre/left/right family anchors after
checking PGXP depth; projected scenery is excluded. Countdown/mirror fragments
stay centred; the bottom-right gauge sprite, needle, digits and redline marker
share the right edge. The mirror draw-area adapter applies the same squash to
the host clipping rectangle while preserving guest draw-area registers.
The classifier is gated to the race state and leaves 4:3 unchanged.

Graphics adds a conditional Depth of field distance submenu with Near,
Mid-range, Far, Very far and Horizon choices. These map to maximum-blur depths
8000, 16000, 30000, 60000 and 100000 camera units, independent of blur strength.
The default Far preserves the previous reach. Both controls disappear when
DOF is disabled. The distance setting persists with the other graphics options.

## Menu presentation (0.2.2)

Widescreen presentation now uses Rage Racer's scene state rather than GTE
activity to identify menus. All selection/settings/title screens use canonical
4:3 projection, no widescreen HUD transformations and the existing black-bar
presentation path. Races (state12), the driving intro (state30), and the prologue (state32)
remain full width, including video playback.
The prologue explicitly remains wide during sprite-only passages as well.
The user-selected aspect ratio and window dimensions are retained, so entering
a race restores widescreen automatically without altering stored preferences.

## Pre-race briefing and fade (0.2.3)

The menu-to-race capture reproduced a state10 loading card that persists into
state12. The old per-piece race HUD anchors moved its title to the left and
its currency suffixes to the far right. Loading state10 now shares race-wide
presentation; all its 2D parts use a centred canvas. During state12 the
translucent variable-size briefing sprites in the central briefing band
(y64..190) retain that centre anchor. Ordinary race HUD sprites retain their
existing edge anchors. This keeps the briefing's physical text width equal
across loading and race presentation at the same display height.

The captured fade is a full-screen0x62 TILE (0,0,320,240). The HUD rectangle
path previously ignored its extent and reduced its width to120 at32:9.
Full-screen masks now bypass both explicit-tag and auto-widget scaling in the
mono-rectangle path; auto rectangle classification also rejects full-screen
coverage before HUD anchor lookup. Existing native-wide expansion remains.

check_race_transition.py captures a real isolated saved-menu-to-race sequence,
including composed PNGs and original packet dumps for before/after comparison.

## Framerate options (0.2.4)

Modern 3D is always active through application settings, including legacy
host_3d=0 files and settings reset. The toggle is removed. Framerate adds
Unlimited, represented by fps=-2. Main disables swap sync and configures the
render thread with refresh_hz=-1. Valid uncapped plans have no scheduling
interval or frame-count ceiling; the existing phase deadline and new-work /
backpressure guards terminate them. Other limits and guest timing are intact.
See Port/data/UNLIMITED_VALIDATION.md for actual validation and known test limits.

## Material cache residency (0.2.5)

The course/vehicle/window working set exceeded the256-tile native atlas,
causing repeated texture decoding without source changes. Capable hardware now
uses512 tiles in16 columns and32 rows; hardware limited to4096-sized textures
retains256. The hot material lookup uses four-way sets, validating slot keys on
every hit. Dirty-source invalidation remains. Extra GPU storage is80MiB when
expanded. See Port/data/MATERIAL_CACHE_VALIDATION.md for matched metrics,
mixed overall-FPS results, hardware limitations and the completed-lap check.

Extreme Oval supplementation is feasible after aligning its spatial mesh by
(0,0,-81920), but is not enabled. Exact deduplication alone leaves10 overlapping
triangles and cannot safely replace separate material-bank/prop handling. See
Port/data/EXTREME_OVAL_FEASIBILITY.md and the reproducible audit tool.


## 0.2.6 Oval environment supplement

`rage_oval.cpp` imports the user's own Oval terrain and immutable texture bank
at bundle preparation, derives alignment from shared face shapes, subtracts
existing surfaces with UV-preserving convex clipping, and appends only missing
terrain to the drawing course. `course` remains the physics/ground receiver;
`drawing_course` is captured in view history and includes optional scenery.
Texture page bit 15 is host-only and routes the native material decoder to an
immutable Oval source under key bit 50. It must survive projected-triangle
render-thread recording. Guest VRAM writes must never invalidate those pages.
See Port/data/EXTREME_OVAL_IMPLEMENTATION.md and OVAL_SCENERY_VALIDATION.md.


## 0.2.7 local project integration

The user-owned Other PC project's compiled texture snapshot module is imported
in Src. Immutable full source banks are selected using original wanted-page
state. TPAGE high7 bits are host source IDs (0 live,2..49 course/class/bank,64
Oval), all must survive render-thread recording. Atlas keys include all7 source
bits; guest uploads cannot dirty immutable pages. CPU sources live for the app.
Terrain +0x15 signed depth bias uses scene flags16..23, then high8 bits of the
native raw-mode attribute; low bit remains raw sampling. Shader bias is bounded
in world units; do not restore blanket blended-pixel polygon offset.
Contact shadows use active-ground sampling and captured inverse view/model
matrices. Normal route filtering only affects donor scenery. See
Port/data/LOCAL_PORT_RENDERER_VALIDATION.md for complete tests and rejected
shadow-winding/cache experiments.

## 0.2.8 tunnel visibility, UI precision and contact shadow depth

The course decoder retains the original cell region/visibility tables and
labels native sectors by their world cell. Submission unions visibility at
both camera endpoints before frustum checks. Other/native_visibility.c is
compiled directly; its original tests cover reversed mesh row confusion,
region bounds and nonfinite camera inputs. Oval supplement sectors keep
cell=-1 and frustum-only visibility.

Screen glyphs and loading solid rectangles now use ordered projected
triangles to retain fractional screen coordinates. UI receipts carry UI1024
and remain point sampled and outside scene depth. UI|SOLID receipts route to
the existing precise flat triangle path, preserving original RGB555 and PSX
blending. Fullscreen fades retain their full-width mask path. Loading border
outer/inner edges therefore retain the same fractional pivot and width.

Original contact shadow plates retain a three-world-unit clearance above the
sampled native road (positive Y points down); tiny bounded depth bias alone
was insufficient close to the camera. Classification and projection validity
are separate. No guest simulation or shadow back-face override was added.
See Port/data/TUNNEL_UI_SHADOW_VALIDATION.md for measured regional timings.

## 0.2.9 full-distance native path and subpixel seams

Legacy region masks were removed from native terrain visibility; sector
frustum and131072/196608 distance limits are authoritative. Highest car meshes
are mandatory; track geometry and texture snapshots remain full resolution.
Vertical terrain faces retain authored front-facing orientation, reversing
in mirrored views, instead of drawing their darker rear faces over the wall.

Main terrain carries GPU_CLIP2048 and enters rage_native_terrain_triangle,
which batches immutable local geometry, material slots and camera receipts
without generic PS1 screen-triangle preparation. Generated terrain bypasses
CPU projected-coordinate reconstruction. Mirrors retain exact projection
provenance for inset bookkeeping. Native stencil bit7 preserves earlier inset
colour against later main3D rendering; original PSX stencil bit0 is separate.

Hardware anisotropic sampling of immutable terrain uses texture level zero;
there is no source texture LOD reduction or atlas mip generation. Live car
textures retain class-separated sampling. Per-store diagnostic histories are
opt-in so read-only profiling and normal play avoid unconditional recording.

Sky projection uses the same horizontal aspect ratio as the world. Its local
angular yaw and4:3 behavior stay unchanged. Position-only bounded seam repair
preserves triangle UV seams, excludes raised/biased layers and zips compatible
near T-junction boundaries. Conservative GPU raster coverage, where supported,
fills subpixel opaque-terrain cracks without changing vertex positions. It is
excluded from vehicle, UI, decal and translucent batches.

See Port/data/OTHER_RENDERER_REVIEW_029.md and029-performance-comparison.json.


## 0.2.9 installed and verified — 20 October 2026
This entry supersedes earlier pending 0.2.9 settings and provisional measurements.
Shareable/RageRacer.exe SHA256: b7581feb89d464bd4af53dcb5c668362ac1cff722387ce297479e1a71b233a7b.
Receipt: Port/data/029-installation.json. Previous executable backed up at the receipt location. data.dat, both memory cards, isolated-BIOS marker and graphics-options.ini hashes are unchanged. Bundle verification returned 0.

Final renderer uses 128 native mesh cache entries and up to 1024 retained material slots (512/256 fallback for smaller GPU limits). Conservative raster coverage is restricted to opaque terrain faces containing open boundaries; ordinary terrain, cars, decals, UI and transparency do not use it. Source-position stitching preserves separate UVs and excludes incompatible raised layers. Coastal source geometry already aligned: its remaining subpixel cracks are addressed through GPU coverage on supported NVIDIA hardware, not claimed as displaced-vertex repairs. Other hardware retains geometric seam repair.

Sky cloud projection now follows the horizontal/vertical projection ratio, preserving 4:3 angular motion in wider modes. Untextured gradient bands separately extend to fill the full width. Restored terrain range is 131072, or 196608 with 1.5x enabled; final 4:3/off and 32:9/on captures confirm these limits and mandatory highest LOD. Incorrect rear-facing wall overdraw is culled; roads remain double sided. Main terrain transformation and clipping run on GPU; mirror provenance retains CPU bookkeeping. Guest game logic and remaining geometry construction still execute on CPU.

Validation: all five Release native tests passed, including watertight seams and 24 Oval course fixtures. Final complete circuit: final-029-1024-circuit, 235 samples, 125 material checks with zero failures and zero GL errors. Final 4:3 and 32:9 short checks each passed 125 material checks. Fresh ultrawide transition captures reviewed for sky/UI.
Renderer timing means at scale8/aniso16/32:9/Unlimited, extended range enabled, effects disabled: first tunnel GPU 4.335ms baseline to 0.491ms; last tunnel 6.420ms to 5.504ms. CPU renderer first 2.325ms to 0.497ms; last 3.960ms to 3.929ms. These are renderer timings, not whole-game FPS or guarantees of eliminating all stutter. Baseline historical symbol map unavailable; do not use its host symbolic samples. Diagnostic aspect restoration was corrected in final runs. See 029-performance-comparison.json for results.

User-owned Other sources reviewed in OTHER_RENDERER_REVIEW_029.md. Retained immutable GPU geometry/materials and draw batching were adapted; replacing the full guest game loop or suppressing PGXP was rejected because it broke cars and mirror provenance. No data.dat or user-save replacement.


## 0.3.0 installed — native mesh-template submission (20 October 2026)
Installed Shareable/RageRacer.exe SHA256 f3c8728de16e69edaeb86c301ceff0a0dff5b4a07d82f5fa3b7830c557ffdb4e.
Receipt: Port/data/030-installation.json. Previous 0.2.9 executable backed up at the receipt's backup path. data.dat, both memory cards, isolated BIOS marker and current graphics settings remained byte-identical. Bundle verification returned 0.

Adapted user-owned Other/rage-racer-pc-main's separation of immutable meshes from scene/camera instances. Detailed source references and implementation: Port/data/OTHER_RENDERER_REVIEW_030.md. Main terrain now prepares immutable local-coordinate/UV/material/classification templates once after all topology, donor clipping and decal work. A copied command carries up to 128 compact triangle templates and one camera receipt. The consumer initialises that camera once per batch and expands into the existing material/frame-generation path. This reduces native main-terrain command payload from three full scene receipts per triangle to a 96-byte triangle plus a shared camera header (roughly 90% less payload for large groups); it does not remove all original guest rendering work or all GPU uploads. Mirrors retain their original provenance path. Both palette variants, texture source, authored texture windows, fades, blend ordering, wall culling and conservative boundary coverage are preserved. No settings or visual-detail reductions.

Validation:
- Five Release native tests passed, including seams and 24-course donor fixtures.
- Final complete circuit: templates-030-final-circuit, 236 samples, route296 through wrap332 back to295. 125 driver material checks passed, zero failures; 300 retained GL events had error0. Reference and initial experiment also completed full circuits.
- High-quality scale8/filter3 checks passed in 4:3 (far131072) and 32:9 (far196608), mandatory highest LOD0, each125 material checks and no GL errors.
- Initial stationary full frames were pixel-identical (1,792,000 pixels). Final camera-reuse build's static scene also matches exactly across1,528,000 pixels, excluding the animated mirror panel and tiny moving distant AI region because captures were wall-clock sampled at different simulation ticks. Details and masks: 030-pixel-equivalence.json. Do not describe the final whole frames as identical.

Same-settings full-circuit A/B: scale4, filter0, AA3, 32:9, Unlimited, extended distance on, screen effects off; identical isolated graphics ini hashes and host profiling options. New final run125.52s, process CPU211.36s, guest ticks3100; legacy125.66s, CPU212.31s, guest ticks3103. Measured real+generated frame deltas23462 vs22648 (about4% more frames per second), at essentially unchanged total CPU cost. Last-tunnel renderer CPU6.521ms to6.030ms (about8% lower), GPU8.136ms to7.789ms. First-tunnel renderer CPU5.439ms to5.362ms, GPU7.111ms to6.751ms; generated first-tunnel GPU timings varied upward0.753ms to0.975ms. These are single-run observations with run-to-run variation, not guaranteed gains. Full raw comparison: 030-performance-comparison.json. Do not compare these directly with0.2.9's older scale8/filter3 run.

The first compact-command experiment regressed (consumer repeated whole camera copies/memsets per triangle). It was corrected and superseded by camera reuse before installation. Keep this distinction in future comparisons.

Remaining larger architectural work: replace original guest PS1 terrain command construction with verified native scene publication, matching the Other port's PortAfterSceneHandler/GameRenderWorldPublishTerrainGrid approach. Preserve guest-visible registers, scratchpad/GTE outputs, car/mirror precision history and game ordering before bypassing builders. Do not globally disable PGXP or shorten distance; prior experiments broke visuals. Consumer still expands receipts into the existing frame-generation lists, and native buffer cache comparisons are another measured cost. Diagnostic RAGE_TRACK_LEGACY_SUBMIT=1 retains the previous submission path for comparisons; normal play uses templates.


## 0.3.1 installed — native terrain publication (20 October 2026)
Supersedes all pending 0.3.1 notes above. Shareable/RageRacer.exe SHA256 6b9db911661cd2c6b6bfff5b71fa7d2d9e69bf6ef4fad54155bdabb11ed5395f. Receipt: Port/data/031-installation.json. Previous 0.3.0 backed up. data.dat, both memory cards, isolated BIOS marker and current graphics settings unchanged. Bundle verification exit0.

Both main and mirror views bypass original terrain face builders through verified native publication. Mirror shared rotation/translation and precision metadata are preserved; mirror CPU projection bookkeeping remains. See OTHER_RENDERER_REVIEW_031.md. No detail/distance/settings reductions. Seven Release tests passed. Final baseline and native runs each completed a circuit; 125 driver checks passed and zero retained GL errors in each. Scale8/filter3 high-quality 4:3/off and32:9/on+all effects/DOF each passed125 checks, zero GL errors. Far limits131072/196608, highest originalLOD0. Frozen mirror differs on96/60000 pixels by max12 channel levels (presentation interpolation), matching geometry; frozen chase car/shadow differs on3/96000 pixels by max5. Whole frames are not pixel-identical. Earlier fresh prologue and race transition passed before the mirror-only addition.

Final same-executable comparison: baseline126.141s/CPU209.703s/3113 guest ticks/23258 measured real+generated frames; native124.653s/CPU189.734s/3116 ticks/23489 frames. About9.52% less process CPU and2.20% more frames per second in this single-run comparison. First tunnel renderer CPU8.241 to7.790ms, GPU11.156 to10.207ms; last CPU5.876 to5.803ms, GPU6.506 to6.618ms. Generated first-tunnel CPU varied upward5.879 to6.903ms. These modest/variable frame-rate gains supersede the preliminary main-only11% figure, not a claim all stutter is gone. Native terrain packet count6080 vs4487995, with6082 native views including2951 mirror views. Same scale4/filter0/AA3/32:9/Unlimited/extended1/effectsOFF. Raw031-performance-comparison.json.

Performance release installed BEFORE any audio changes as requested. Next authorised work: MP3 encoding in Generate Data and game playback, with backup of original data.dat before replacement.


## 0.3.2 installed — MP3 CD audio (20 October 2026)
Performance0.3.1 was installed before audio work as requested. Installed game+Generate Data+MP3 data.dat; receipt032-installation.json. Previous game/generator/752063497-byte raw archive backed up inPort/data/release-backups/032-audio. New archive279758061 bytes, 320kbps CBR44.1kHz stereo LAME3.100quality0. Full originalTOC/index01/storedpregaps preserved by game.cue.mp3toc. Rawdata track140042784bytes unchanged. Existing original raw archives remain supported when table absent.

minimp3/minimp3_ex CC0 decoder is used by game+generator. The initial dr_mp3 prototype was rejected after real-disc seeks near track ends failed; no dr_mp3 linked/shipped. LAME gapless tags remove encoderdelay/padding. Complete original-disc comparison verifiedall16audio tracks and152695956decodedstereo frames, exactrandom seeks atbeginning/middle/end, unchanged everydata sector;032-audio-verification.log. Synthetic fixture coversTOC/pregaps/quality/exactseeks/malformedtable. Final generator headless exit0, outputbyteidentical tofirst fullgeneration. Final isolatedrace mp3-final-race125materialchecks0fail/GL0, CD tap593292frames/574596audible, gamehostaudioactive. Installed bundleverifyexit0. Usermemorycards/BIOSmarker/settings hashes unchanged.

Dependencies pinned/provenance Src/vendor/audio/README.md. Shareable/Licenses includes notices andMP3-encoder-relink-0.3.2.zip containing fullunchangedLAMEsource plusgeneratorobjects/libs/source/CMake instructions. Relink kit successfully rebuiltwithVS18/2026.

Latest user asked continuation4:40, then explicitly replied JUST DO IT NOW. No automation created; do not schedule. Additional authorised current work: investigate intermittentbluish/transparentterrain inrearviewmirror and replays, and exportoriginalmusic names/mappings tosongs.ini withcustomname+MP3filename underShareable/music selectablewithbasetracks onpre-race musicselector. StillTODO; donotclaim implemented. Preliminaryfadecause: rage_track.cpp markssector known onlywhenpositiveview-spaceZ; turningview/replay canstartscreen-doorfade onnearbyroad revealingbluesky. Needtest/fixwithoutlosingfarfade. OriginalmusicnamesinOther/rage-racer-pc-main/src/port/native_game_state.c; mapping BgmCdTrack:selection1..9 ->CD3..11, selection10 DEEPDRIVE ->CD17;0 RANDOM. Nativeg_BgmTrackCountdefault9/unlocked10, fixedshufflearray mustNOT be expanded. SeeOtherPALmenu/round_screen.c for originalselector. Needindependentnativecustomlist navigation/display/musicrequestmapping; preserveguestarrays/savecompat. No subagents authorised.


## 0.3.3 installed — opaque nearby terrain and configurable pre-race music
This supersedes ALL pending0.3.3 notes. Shareable/RageRacer.exe SHA256 9d97fbed811850ede93726248d42fc892d93a4e5bb15737232e2032d16690706. Receipt033-installation.json; previous0.3.2 executable backed up at C:\Users\Nevada\Desktop\Rage PC\Port\data\release-backups\033-music. Current MP3 data.dat, Generate Data.exe, memorycards, BIOS marker and current graphics settings remained byte-identical. User preference filter2/extended0 is preserved; diagnostics explicitly override isolated settings. Installed bundle verification exit0. No automation was created; user said continue NOW.

Confirmed blue tint/transparency cause: positive-view-Z terrain appearance history forgot nearby road behind the camera and screen-door faded it through blue sky after view changes. Orientation-independent camera-to-AABB history and opaque contact terrain within32768 fix this while preserving optional distant fade. Reference replay-09 visibly shows blue stippling onroad/wall; candidate opaque. Replay appearance events313 versus32 in initial24sec comparison. Replays now correctly use full selected width (scene17 was mistakenly treated asmenu). Main/mirror depth, authored materials, full draw distances and highest LOD remain.

Shareable/songs.ini exports ten original names: song1..9 mapCD3..11; song10 DEEP DRIVE mapsCD17; selection0 RANDOM. Add [song.11] with name=MY SONG and file=MySong.mp3 underShareable/music. Supported44.1kHz stereo MP3<=20min; original ASCII font shows18title characters, up tosong99. Left/right cycles original/custom list; input extends choice time. Reload eachpre-race. Random retains original built-in shufflepool. New independent selector preserves guest count/shuffle/save sizes and uses the existing CD17 channel for custom audio only; dynamicEOF and ordinary restart remain. No custom title/file was shipped: clean10-song template and empty music folder; instructions Music - Read me.txt.

Validation: eight native contracts passed; independent music unit passed Europe/Japan1.1/USA mappings, navigation/text/arraybounds/cleanup. Exact final executable complete circuit: final033-matched-circuit,232 samples, route296 through wrap back296, 6056 native views including2938 mirror views, far196608/LOD0. Final high-quality4:3 custom test far131072: 871 customsector reads/4 complete restarts, actual title11 CUSTOM SONG rendered and audible. Final high-quality32:9 recorded replay: 41 samples, allscene17, fullwidth/opaque road. Allthree runs125materialchecks each,0failures,0retainedGLerrors. Raw033-validation.json.

Final full-lap observation(scale4/AA3/filter0/32:9/Unlimited/extended1/effectsOFF): wall124.174s, processCPU204.500s, guestticks3104, measuredreal+generatedframes22536. This is functional/profiling evidence, not a controlled speedup comparison against builds from earlier sessions. Earlier final033-circuit inherited current userfilter2; do not label it filter0. Music dispatch work is gated topre-race; live races avoid the additional helpercall on everyguest dispatch. Performance0.3.1 publication release and MP3 0.3.2 were installed and verified before this follow-up.

Current user requests completed. Remaining architectural possibilities from earlier notes are future improvements, not unfinished acceptance requirements. Do not resume old pending steps or schedule stale continuations.

# Free run texture and road-contact correction

Bind both retained texture banks per surface using that donor's render-table
section bounds and its event walk-start offset. The active course needs the
same treatment: its stock global texture swap cannot choose textures for a
world containing several stretches at once. Donor and active faces now retain
their own course and bank through clipping and native terrain publication.

Road selection measures authored curvature centres, tangent angles, asymmetric
lane widths and camber, including segment end planes. It accounts for car-hull
clearance and predicted motion before choosing a junction exit. The old chord
search could select an adjacent segment whose original collision clamp then
pushed the car. Only player lap-progress placement selects a road ring; the
original FindTrackSegment routine handles other calls without changing global
road pointers.

Regressions cover circular-road offsets, both texture banks on BIG/MID/HI,
authored curved centreline sweeps, lane clearance and bridge height. Live checks
sample the unique stretches and tight curves in all four road networks.

# Free run unified world

Free run combines BIG, MID, HI and the actual OVAL5 road into one retained
terrain mesh. OVAL1 contains BIG's placeholder route. Match authored surface
shapes to the loaded course once, keep those alignments through the union,
and translate each corresponding road point and curvature centre together.
Subtract coplanar donor surfaces from the existing union, interpolate UVs on
clipped edges, repair seams, and omit raised barriers within any road corridor.
Separate corridor sentinels prevent invented links between road rings.

Keep original course texture snapshots per donor. Terrain source bits survive
both immediate and retained OpenGL submission. The minimap fits all four rings;
player and ground sampling use the combined world.

Free run's native FindTrackSegment / AccumulateLapProgress overrides select an
original road ring by position, stable rendered height, lane width and predicted
motion, with hysteresis on shared roads. Original 24-byte road points and arc
centres are published in trusted expansion memory. Retail temporarily uses the
primary car-height word as contact scratch, so it cannot select an elevated road.
Keep the original track-length range for bounded event and texture indexing;
Free run continues to bypass lap timing, finishes and record writes.

Validation includes all original native contracts, fixture-based mesh assembly,
continuous sampling of every road, shared junctions, and a regression with the
transient height pointer observed during live driving. Disc assets remain local
and are imported from the user's verified game data at runtime.

# Live race minimap (0.3.22)

Read the complete centreline already decoded with each course and cache an
antialiased white track bitmap. A uniform X/Z fit preserves course shape.
Capture player position/yaw and all 11 rival records once per race tick, filtering
the original activeFlag/aiEnabled fields. Regional rival array bases are verified
against the generated EU/JP/US code, rather than assuming a common RAM offset.

CPU-owned ARGB images are published under a mutex; the OpenGL presenter never
reads guest RAM. Composite the transparent map once into each presented frame,
at middle left within the fitted game area, before host menus and notifications.
Keep existing opaque OSD behaviour; only the minimap path enables alpha blending
and linear texture filtering. No new GL objects or game resources are required.
The map is cleared on track reset and hidden in non-race terrain views.

# Animation-ordered AI trails (0.3.21)

A captured trace showed future real-frame lamps being submitted between older
generated poses (for example tick 3973, followed by tick 3971 at phase 0.792).
Wall-clock history turned this render scheduling into false left/right motion.
The live endpoint could also connect to future samples despite the older-tick guard.

Terrain camera receipts now carry the previous tick span and region-specific
25/30 Hz animation rate. Order samples by interpolated animation time, replacing
duplicate timestamps, and omit samples newer than the frame being drawn. Measure
screen speed from chronological net displacement over a short window, keeping
the 90/60 thresholds. Framebuffer origins are removed while storing points and
restored while drawing, sharing histories across alternating 320x240 draw pages
while keeping separate subviews distinct. Large screen jumps reset the path.

Regression checks reproduce future-frame-first submission, reject future points
from older ribbons, and verify continuous history across framebuffer page flips.

# Continuous source paths with visibility gating (0.3.20)

In title-owned sky receipts, recolour only the flat untextured lower sky quad
to #0c0c0c. This is the below-world fallback visible through track gaps;
gradient bands, textured sky tiles, UI and general VRAM clears remain unchanged.

Keep up to 64 consecutive projected samples per source/view. The filtered
90/60 screen-speed gate controls per-sample exposure, while raw projected
positions preserve the actual lamp path. Connect the ribbon to the live source
each frame; retain shared joins and the 0.18-second fading tail. Ignore transient
one/two-tick receipt regressions rather than repeatedly erasing valid paths.

Batch ANY_SAMPLES_PASSED queries against completed scene depth for small lamp
centre patches, including the native-wide target. Issue all queries before
reading their results. An obscured lamp draws no halo or trail and clears its
history; hidden motion cannot create a streak when the source becomes visible.
Ribbon fragments also use scene depth. This replaces the unoccluded lens overlay.

Initialize gl_ClipDistance[0] explicitly in the lamp shader: it is enabled during
runtime lamp drawing and previously undefined. Driver checks now enable the
runtime clip state, test visible/blocked query results, and verify live attachment.

# Lens-stain persistence and screen-speed gate (0.3.19)

Rear histories track filtered projected motion (25 ms position smoothing,
40 ms speed smoothing). Deposits start above 90 native screen units/second
and stop below 60; a one-unit spacing dead zone suppresses subpixel geometry.
The gate measures screen motion, including camera movement, rather than car speed.

Render deposited stains separately from depth-tested lamp halos. Stain positions
and birth times remain fixed, are not reattached to the moving lamp, and fade
over 0.18 seconds even when lamp receipts disappear. Stains draw as a lens overlay,
so moving scene geometry cannot repeatedly hide and reveal the existing smear.
No fresh deposits are made by headlights or outside tunnel lamp activation.
Driver checks cover slow-motion rejection, fast-motion activation, gap reset,
fixed stain positions and independent fading.

# Softer trails and near-road beam (0.3.18)

Rear ribbon RGB intensity is 0.65 instead of 1.25, without changing sample
history, width or fade duration. Correct the headlight direction from 12 degrees
upward to 24 degrees downward in mesh Y-down coordinates. Both body-receipt and
cockpit-fallback emitters use the corrected aim. Surface brightness, range and
the upper cutoff are unchanged. Two driver checks verify that the new aim
reaches a nearby receiver missed by the previous beam.

# Continuous rear ribbons (0.3.17)

Build a continuous polyline from projected history plus an unsampled live lamp
endpoint. Adjacent quads share bounded miter joins, eliminating cracks and
overlapping segment ends. Interpolate the oldest point at the 0.18-second
boundary and use a smoothstep age fade, rather than deleting entire segments.
Duplicate projected positions are removed before tangent calculation.

Use current lamp depth consistently along the screen-space ribbon, with a small
bias toward the camera; scene depth testing still occludes solid geometry.
Additional graphics checks cover shared joins, live attachment, the fading tail
boundary and consistent ribbon depth.

# Projected rear trails and headlight cutoff (0.3.16)

Rear trails keep 24 projected samples per lamp and viewport rather than world
positions. Remove the speed gate; stationary screen positions produce no ribbon,
but either object or camera movement does. Histories expire after gaps, reverse
ticks, teleports and large projected jumps. Ribbons are five native pixels wide,
brighter and last 0.18 seconds; depth testing remains active.

Headlight surface upload uses a 0.35 RGB multiplier. In the surface shader,
headlight-range emitters reject negative world-Y displacement (world Y is down),
and feather across the first 32 units below lamp height. This prevents upward
spill even on slopes. Rear lights retain their original spill and lens intensity.
Driver probes cover below/above cutoff receivers and stationary-world screen motion.

# Headlight tuning (0.3.15)

Front lamps no longer contribute motion ribbons in either body or cockpit
fallback paths. Apply a 0.85 multiplier to headlight RGB during surface-light
upload only; lens emission, halos, rear lamps and lamp activation are unchanged.

# Active lamp spill and chase-view trails (0.3.14)

Restore the bounded nearest-16 surface-light upload from active lamp receipts.
Disabled lamps never enter this list. Both front and rear lamps use the existing
tunnel/speed trail gate. Direct lens halos retain their directional falloff;
motion ribbons scatter in all viewing directions and retain scene depth testing.
This allows headlight and rear-light histories to appear in following views
without making walls or car bodies transparent.

# Car-lamp scope correction (0.3.13)

Automatic headlights and tail lamps require tunnel shelter. There is no
daylight tail-lamp baseline. Brake lamps follow actual brake input with no
release fade; tunnel exit clears automatic lamp strength immediately. The
existing speed-based rear trails remain restricted to tunnel lighting.

Runtime lamp binding always sets the surface spotlight count to zero. Only
vehicle lens masks, depth-tested lamp glows and tunnel trails receive the
added lighting; roads, scenery and tunnel fixtures receive no car-light spill.

# Car lamps and tunnel trails (0.3.12)

Sources adapted from the reference: `src/render/car_lamps.c`,
`src/render/car_lights.c`, `src/port/modern/modern_material_uniform.c`,
`src/port/modern/shaders/native_spot.glsl`, the lamp-glow shaders, and the
car publication and lampsOnly path in `src/port/render_world_game.c`.

The compiled C lamp library preserves the reference's per-car positions, UV
rectangles, round-lens masks, player variants, rival banks and verified course
bank aliases. CPU receipts identify the actual body entry and asset family,
with no lamp metadata attached to wheels or legacy shadow plates. Player
asset selection follows the original base-variant table and owned grade;
unsupported banks simply do not receive guessed lamp locations.

The CPU reads each car's real brake input at +0x15E and its own track progress
at +0x70. Shelter follows the retail zone table, including reversed-series
progress, entrance/exit blends, no-blend zones and exit-only zones. Address
mappings come from the reference PAL/JAP11/USA symbol maps. Lights follow the
reference 0.6-second dark latch, 0.28/0.36 hysteresis, 0.2-second headlight ramp,
20%-day/40%-tunnel tail intensity and 0.1-second brake release. Automatic
activation currently follows tunnel shelter, rather than the reference's
additional sky-daylight animation.

Immutable car receipts travel with scene vertices and queued pose palettes.
Sixteen mutex-protected player pose snapshots supply the reference's lampsOnly
behaviour when cockpit rendering omits the player's body. Render-thread
lookup uses the terrain receipt's exact guest tick and interpolation phase;
it never reads guest RAM. Player origins retain the reference ground clamp
and model horizon, and body orientation retains the reference Y/X/Z ordering.
Visible body receipts take priority over the lamp-only fallback.

Lens UV masks are additionally constrained to the matching pose and nearby
source lens position, since runtime atlas IDs differ from reference material
indices. Emission uses bounded headroom compression on the LDR framebuffer,
so tunnel tail lights do not saturate to the same red level as stop lamps.
Local spot illumination uses the reference cone/range falloff and compressed
light accumulation. The nearest sixteen relevant lights are bound per group,
with range rejection before per-fragment normalization. Halos use the reference
facing rule, Gaussian profile and outward depth offset. They are depth tested
and batched per view, preserving mirror exclusion and widescreen clipping.
Local spot lights do not have a separate ray-traced occlusion pass.

Each rear emitter has a bounded sixteen-point history sampled at intervals of
at least 8 ms. Ribbon geometry connects those world positions with continuous
cross-trail falloff and age-based fading, rather than separate dotted halos.
History is limited to 0.12 seconds; speed influence starts at 80 km/h and
reaches full strength at 160 km/h. The tunnel/headlight state gates trails.
Snapshot reversals, gaps and large position jumps clear the history. Opaque
scene depth hides both halos and ribbons; the overlay does not write depth.
All buffers, shader resources and histories are released with the renderer.

Validation: fourteen compiled contracts pass. New contracts cover dark-entry
hysteresis, short bridge shadows, lamp fade timing, braking, speed gates,
player variants and rival profile mappings. The final driver probe contains
144 checks, including local-light cone/range rejection, spatially constrained
lens masks, actual halo shader depth occlusion, and additive blend restoration after a subtractive game effect. It passes at 4:3, 16:9 and
32:9. The isolated 125-second circuit reported zero track errors. The final
tunnel regressions demonstrate cockpit headlight activation, chase-camera
ribbons at speed and automatic light exit; captures and JSON receipts are in
`Port/data/car-lights-{circuit,tunnel,trail-check,final}`. The circuit precedes
only lens-emission/LDR refinements and diagnostic live counters; the final
binary has separate driver and tunnel checks. The final additive-blend regression also passes in all three aspect ratios. No performance speedup is claimed
for the added lighting. Existing data, memory cards and preferences are kept.
# Native input caching, light-pass filtering and terrain lighting (0.3.11)

Reference code inspected: `src/port/modern/modern_native_gpu.c`,
`src/render/render_shadow.c`, `src/render/render_world.c`,
`src/render/render_stage.c`, `src/render/render_mesh_build.c`, and
`src/port/modern/shaders/native_{shadow.vert,shadow_masked.frag,texture.frag}.glsl`
in `rage-racer-pc-main`.

The retained-mesh backend now owns one VAO per cached mesh. Upload establishes
its five vertex attributes once; regular and intermediate frames bind that
VAO instead of issuing five enable and five pointer calls per group. Shadow
index bindings are explicitly reset when a group is rebound. Eviction and
shutdown release both vertex-input and caster-index resources.

Scene palette allocations are retained by the bounded 1024-slot draw queue,
with at most 48 poses * 80 bytes per slot. A slot grows only when needed and is
freed on renderer teardown. Camera lookup first collects the first terrain
receipt for each distinct draw area, preserving previous selection semantics,
then shares its inverse across groups in that view. Mirrored views stay separate.
Only populated view records are initialized, avoiding clearing a 68-KiB scratch
array on each drain.

For mixed meshes, a cached index buffer selects only triangles whose provoking
vertex carries the vehicle-caster bit. Indices refer to the existing packed
vertex buffer; attributes, triangle order, texture alpha, STP rejection, culling
and fade coverage remain unchanged. Fully casting meshes reuse their existing
index stream. Allocation failure retains the full-stream depth-pass fallback.
The reconstruction contract checks both expanded and packed source indices,
non-caster omission, provoking-vertex selection and incomplete triangles.

Terrain uses the reference stage ambient (0.35,0.35,0.38) and diffuse
(0.85,0.85,0.80) colours with the existing converted reference sun direction.
Normals are reconstructed from world-position derivatives because the captured
retail stream does not supply authored normals. Absolute facing makes the
retail two-sided surfaces consistent through main and reflected cameras. A
horizontal lit surface is the brightness reference; 50% influence preserves
baked colours instead of adding a second full-strength light. Shadow visibility
separately attenuates ambient to 55% and direct light to 10%, following the
reference shader. Existing cars, sky, HUD and decals retain their prior colour
paths. This does not import sky-driven sun animation, spotlights, PBR materials,
vehicle self-shadowing or the reference ray-tracing structures.

All screen derivatives for receiver-plane correction and normal reconstruction
now run before any fragment discard. The depth filter obtains its dimensions
from textureSize rather than assuming 4096; the actual 4096-square runtime map
is unchanged. This also makes the two-square driver fixture exercise the same
bilinear comparisons correctly. Six new driver cases verify the lighting and
shadow split with both ordinary and reflected inverse matrices.
Validation: all 13 compiled contracts pass. The final executable passed all 136
driver probes in a race and at 4:3, 16:9 and 32:9. The baseline and final build
ran sequential 125-second assisted circuits from the same isolated snapshot,
covering the course segment range with zero reported track errors. Graphics
preferences were copied from the current release. These are sampled runs, not
a deterministic frame-for-frame benchmark; pacing and trajectories vary.

Excluding the first five seconds, median real-frame CPU time changed from
5.999 to 5.711 ms and generated-frame CPU from 2.229 to 2.103 ms. Median real
GPU time changed from 7.758 to 8.180 ms with the added lighting, and generated
GPU from 1.198 to 1.112 ms. Real CPU p95 changed from 8.215 to 7.605 ms;
generated CPU p95 increased from 3.749 to 4.327 ms. Real GPU p95 changed from
12.757 to 12.201 ms; generated GPU p95 increased from 3.507 to 4.269 ms.
This supports lower median CPU overhead, not an across-the-board frame-rate
or hitch-elimination claim.

The final circuit queued 266,069 groups using 250 palette allocation/growth
operations and 29,840 view inversions. Its light pass submitted 24,631,713
vertices instead of 25,076,493; 444,780 non-caster vertices were omitted.
The separate aspect check also exercised the chase camera. Logs and captures
are under `Port/data/reference-lighting-{before,after,final,aspects}`; the
intermediate `after` run predates retained VAOs. The old executable is backed
up under `Port/data/reference-lighting-backup`.
# Wheel and shadow stability follow-up (0.3.7)

Opaque rotating wheel faces now bypass the original integer-screen winding
rejection, as body occluders already did. In the diagnostic race, rival front
and rear wheels used GT4 handler 0x29230 and frequently had integer NCLIP area
zero. Discarding these faces made the displayed wheel and its light-map caster
change with the coarse source projection. The unrounded native vertices now
reach GPU clipping and body/wheel depth testing consistently. The scope covers
opaque GT4 and opaque flat wheel surfaces; blended flat commands, shadow plates
and rival far bodies are excluded. Existing EU/JP/US handler mappings are used.
The original source wheel animation, textures, steering and physics remain.

The shadow map now follows the camera when the player's body is not submitted,
matching the reference fallback. It no longer assumes that transform palette
zero belongs to a vehicle: the first palette entry can change between scenery
and different rival cars as painter order changes. An explicitly tagged player
part still takes priority. A compiled regression varies the non-player palette
while requiring the fallback centre to remain unchanged.

Player shadow identification also uses its actual fixed shadow entry (1),
separately from the rivals' five-entry model assemblies. A player rear-wheel
or blur bank such as entry 6 must never be classified as a rival shadow plate.

Validation: all twelve compiled contracts pass, including new wheel-stream and
shadow-entry cases. The 26-second AI/mirror race retained 325248 wheel faces,
including 292636 whose integer projection collapsed; it reported zero track
errors and passed all 130 driver probes. Final-build race and 32:9 recorded
replay checks are in `Port/data/cars037-final-race` and
`Port/data/cars037-final-replay`. Replay-only stock draw distance remains intact.
Diagnostic wheel-handler logging was removed before the release build.

# Reference vehicle integration (0.3.6)

Reference sources used exclusively: `src/port/render_world_game.c`,
`src/main/PAL/main/render/draw_player_car_model.c`,
`src/render/render_world_frame.c`, `src/render/render_shadow.c`, and
`src/port/modern/shaders/native_{shadow.vert,color.frag,texture.frag}.glsl`
in `rage-racer-pc-main`.

Cars retain their individual body, rear axle and two steered front-wheel
receipts, authored palettes, zone lighting, fog, banked suspension transform,
steering and speed-dependent wheel/blur banks. The recovered wheel assembly
already matches the reference's banked wheel-centre calculation; it must not
receive a second bank rotation. Body and wheel transforms now interpolate in
world space before the camera is applied, matching Render World's ordering.
Camera bases and origins are immutable endpoint receipts. Exact real-frame
endpoints are unchanged. The reference's min(carY,modelY) ground constraint
applies consistently to the native body/wheel assembly, without changing
physics or guest GTE state. Ground offsets also retain distinct endpoints.

A 4096-square directional light map now uses captured opaque body and wheel
geometry, with alpha-tested texture holes. The light direction, 4096 game-unit
extent and texel-snapped origin follow the reference; PSX vertex coordinates
are four times game-world units and invert Y/Z relative to the reference's
render world. The map follows the player when available. Cars visible in main
and mirror cameras share the same world-space light. Terrain and scenery use
four-tap bilinear PCF with the reference slope-dependent bias, plus receiver
plane depth correction. HUD and sky do not receive these shadows.

Integration limit: vehicle materials keep their authored GTE lighting and
paint rather than receiving the map on their own thin body panels. This
avoids self-shadow striping in captured retail geometry. The reference's full
PBR coat/reflection and ray-tracing stack is not imported. Body and wheels
still cast their actual silhouettes onto the world. The legacy flat plate is
suppressed when a valid native map is available; classic mode and incomplete
native scenes retain their existing fallback.

Native scene groups are queued until the next non-scene operation so the light
pass can precede terrain shading. Retained GPU meshes are pinned until drained;
only transform palettes are copied. UI, depth-of-field, copies, fills, texture
replacement, frame completion, resizing and shutdown preserve painter order by
draining the queue. Material and mesh identities remain independent of camera
motion. No user settings or assets are replaced. `RAGE_VEHICLE_SHADOWS=0` is an
internal A/B diagnostic, not a user-facing graphics option.

Validation adds compiled world-space camera-orbit/wheel attachment and light-map
snapping/mirror-inversion contracts. The driver probe now checks light-depth
writes, caster rejection, shadow/lit reception and contact bias alongside the
existing body/wheel occlusion and material probes (130 checks total).

Verified executable SHA-256:
`6ce0ab1bf66e69eba05e8aba0bf91eea77c11183caceef3fcc32d1a22d46e0ef`.
The final build also drains pending native work before switching Modern 3D
on/off; a final race smoke and all 130 driver checks passed after that lifecycle
change. The full-lap/replay/effects runs precede only this drain-order follow-up.
All twelve compiled contracts pass. Driver probes pass all 130 checks in a
16:9 full lap (including 38 native mirror views), a 32:9 recorded replay and 4:3 at 8x scale with screen effects,
and depth of field. The lap covered 118.907 seconds, roughly 119.1
output frames/second including generated frames; all 218 sampled track-error
counts were zero. This is a quality-change validation, not a matched baseline
speedup claim. Normal extended distance stayed 196608; replay stayed at 131072
while retaining the user's enabled preference. Evidence is under
`Port/data/cars036-full-lap`, `cars036-replay-final` and
`cars036-final43-effects`. Frozen A/B captures verified shadow-map activity and
an unchanged gauge/HUD region. Intermediate self-shadow experiments were
rejected; car panels keep their existing lighting in the shipped build.

# Starter clipping and replay visibility (0.3.5)

Native geometry now clips at camera depth 1 rather than 8. An explicit GPU
clip distance and depth clamp separate this near plane from the existing
depth mapping, preserving the established road/decal and wheel/body depth
precision. Geometry behind the one-unit plane still clips. The driver probe
now checks visible depth-2 geometry and rejected depth-0.5 geometry, alongside
the existing terrain, layer, filtering and indexed-draw checks.

The reference implements the starter in
`src/main/PAL/main/track/draw_start_grid_scenery.c` and
`src/port/render_world_game.c`: a small vertical lift and a dedicated -2048
depth bias preserve retail ordering against the road. Its native vertex shader
bounds that bias in world units. The port now carries an explicit starter
material attribute through retained meshes and applies the same -2048 bias,
with the reference's quarter-unit scaling and bounded-depth formula. Other
materials retain their established depth treatment. The original face selection,
simulation and GTE transform remain intact. Native grounding remains scoped to
this actor. The temporary source rejection bounds are restored exactly on return.
Verified 16:9 and 32:9 captures in `Port/data/starter-reference-depth` and
`Port/data/starter-reference-depth-wide` show both legs continuing to the frame
edge. This addresses road occlusion rather than assuming the camera near plane
alone caused the missing legs.

Replays (scene 17) now use the original course mesh and original selected-sector
list, without the additional native terrain supplement. The 1.5x scenery range
override is suppressed in replays, and native terrain uses the stock 131072-unit
far limit regardless of the saved extended-distance preference. Normal racing
retains its selected 131072/196608 range and native terrain supplement. The
preference itself is never rewritten. Intermediate frames retain each real
frame's original replay sector selection.

All ten compiled contracts pass. Driver probes report zero failures.
`replay-stock-verified` verifies a recorded replay with extended distance enabled:
the saved option remains 1, effective far range is 131072, and
`replay_stock_distance` is 1. `starter-final-normal-distance` verifies racing with
the same option: effective far range 196608 and replay policy 0. Both report
zero terrain errors. The replay capture retains the road, tunnel and vehicle.

# Native rendering performance (0.3.4)

The only external implementation reference for this work was the supplied
`rage-racer-pc-main/` tree: `docs/performance-2026-09-07.md`,
`src/render/render_geometry_pack.c`, and the prepared-mesh/render-world code.
Its relevant principles are immutable local geometry, independent camera
transforms, exact indexed payload sharing, bounded residency, and eliminating
intermediate staging copies. This port retains its own OpenGL backend and
regional translated game routines.

Terrain now remains in compact sector records throughout frame capture and
interpolation. Each generated sector prepares its camera once, instead of
copying and preparing three large scene receipts per triangle. Camera continuity
checks and world-object placement accept both the compact and original forms.
The diagnostic `RAGE_COMPACT_TERRAIN=0` retains expanded interpolation records.

Rear-view terrain now shares the prepared/GPU-clipped path with the main view.
Reflection remains in the original camera matrix; reflected wall winding has
its own shader flag, and the inset retains its independent depth and scissor.
`RAGE_GPU_MIRROR=0` retains CPU-clipped mirror submission for comparisons.

Terrain admission writes the final sixteen-float native payload directly,
eliminating the former thirty-two-float staging array and subsequent repack.
Dense pose indices retain camera-independent geometry identity. Allocation
failure reconstructs the compatibility staging payload rather than dropping
geometry. The geometry cache now has up to 512 entries and a guarded hash
lookup, with a 32 MiB source-payload budget. Corresponding retained GPU vertex
and index storage never exceeds expanded source size.

On a cache miss, complete byte-identical vertices share indices while preserving
triangle order and every UV, material, colour, bound and pose attribute.
Indexing is selected only when vertices plus indices use less storage than the
expanded input. Allocation failure retains expanded draws. Index buffers retire
with their mesh and renderer resources. `RAGE_INDEXED_GEOMETRY=0` selects the
expanded GPU path. Frame diagnostics expose source/upload bytes, indexed uploads
and compact terrain record/triangle counts.

Validation on 20 October: the nine compiled rendering contracts pass. The new
contract compares 131,072 packed terrain payloads byte for byte with the old
path, reconstructs indexed input exactly, and checks expanded fallback.
The driver probe compares all 65,536 pixels of independently cleared indexed
and expanded draws in addition to the existing material/depth/filter checks.

Short moving comparisons used the same private PAL race checkpoint, 16:9,
4x rendering, 16x anisotropy, 120 Hz target, unchanged effects and native
25 Hz race updates. Two baseline runs produced approximately 96 and 108
rendered frames/s; the complete optimization candidate produced approximately
119. These rates use generated-plus-real frame deltas per native-frame delta
at 25 Hz, not physical scanout measurements. That is an observed 10–25% gain
across those comparisons, with substantial baseline variability. Median
generated-frame CPU times were 4.536/3.468 ms before and 2.738 ms after;
this is not a guarantee of a 120 FPS floor. One 30-second optimized run uploaded
726,604,524 bytes of resident geometry versus its 1,096,450,752-byte expanded
equivalent, about 34% less. This measures mesh uploads, not all GPU traffic.

Development evidence is retained under `Port/data/perf-reference-*`, including
the intermediate experiments, captures and executable hashes. The optional
instruction sampler was enabled only in `perf-reference-host`; that run is
excluded from FPS comparisons. Visual route captures cover both tunnels and
main/mirror terrain. Resolution, texture filtering, draw distance, car detail
and simulation timing were not reduced to obtain these results.

The final 0.3.4 executable completed the checkpoint-to-checkpoint lap in
124.515 seconds, returning from route segment 296 to 296 after a wrap, with
zero terrain errors. Its rendered-frame counter rate was approximately
119.48/s over the lap, median generated-frame CPU time was 2.589 ms, and
resident geometry uploads were 34.55% smaller than their expanded equivalent.
The first comparison section measured approximately 119.35/s, corroborating
the short candidate run. The actual-driver indexed/material/depth probe
reported zero failures. Evidence: `perf-reference-release-lap/`.

# Native replay and texture-fragment fixes

Race countdown correction (20 October): flat HUD rectangles now retain fractional
horizontal coordinates through the projected UI path, using the same anchor as
textured HUD pieces. The countdown's 2x1 LED cells previously rounded to whole
320-space pixels, merging their one-pixel gaps into bars at wide aspects.
The game rebuilt successfully; isolated race captures at 4:3, 16:9, 21:9 and
32:9 preserve the dotted grid and board proportions. Captures are retained in
`Port/data/countdown-fix-*`. The previous playable executable is backed up in
`Port/data/countdown-fix-backup`.

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


### Free run junction collision boundaries

Free run now reselects the physical route immediately before the retail
UpdateCarTrackState contact routine, updating its segment argument as well as
the active road table. At junctions, boundary insets can be relaxed only when
all four car collision corners lie within the union of adjoining road corridors
at the same height. Road edges without a neighbouring road, and crossings at
a different height, retain the original collision response. Height, banking,
surface response and the remaining boundary checks still use the retail routine.


### Free run signs and bridge preservation

Junction-panel removal now requires a short vertical obstruction reaching road
level. Elevated sign boards, bridge beams and tall supports are retained.
Upper-section visibility classification also preserves scenery within the lower
road corridor, so an overhead structure does not disappear merely because its
centre is nearer an upper road. On the class-1 four-course fixture this retains
128 additional faces (190 additional triangles) versus the previous merge.
Regression checks cover elevated panels, tall supports, sampled authored
structure surfaces, and the existing open-junction/outside-edge behaviour.

### Retained terrain ranges — 23 October 2026

Installed executable SHA256 f2edfb1c040bdf48b1a2e2ae62bbc2ba612a99473520dc0a68330ad3055935d5.
The updated reference renderer informed immutable terrain range identities,
resident GPU sources and adjacent compatible draw coalescing. Visible ranges
are gathered on the GPU; normal hits avoid per-triangle CPU expansion and
full-geometry hashing/comparison. Commands retain complete fallback payloads.
Camera, mirror, fade and light state remain live. Material tile reuse, dirty
materials, settings changes and native allocation serials guard cache validity.
Shadow/player tags are cached with immutable meshes. RAGE_RETAINED_TERRAIN=0
retains the prior path for comparisons. Source retention remains bounded;
extra expanded gather sources and the GPU gather stream add bounded GPU memory.
This is not zero-copy rendering and does not change clipping or draw distance.

Two matched full-lap pairs showed 35.56% / 62.61% higher real-plus-generated
presentation throughput and 19.20% / 7.64% less process CPU. The first pair
included route screenshots; the final exact-executable pair did not. These
are sequential local observations with substantial variation, not guaranteed
FPS gains. All 17 native contracts passed; driver checks passed on race,
high-quality scale8/filter3/effects, replay and five Free run locations, with
zero retained GL errors. Whole game images are not claimed byte-identical.
Details: Port/data/REFERENCE_PERFORMANCE_REVIEW_2026-10-23.md and
retained-terrain-final-verification.json. Previous executables backed up under
Port/data/release-backups/20261023-retained-terrain. Installed main/Updated
executables match; game archive, user cards/settings unchanged; bundle verify
exit0. No GitHub publication was requested in this turn.

### Multiplayer menus and independent car selection � 23 October 2026

The title menu places Multiplayer below Free run. Its indexed label uses the
retail menu artwork; missing U/Y glyphs were reconstructed from existing letter
strokes. Reserved atlas rows preserve the original copyright, logo and menu.

Mode selection reuses the original Time Attack course-menu layout and animated
chrome. Course name/length, the diorama and car statistics are hidden. Start,
Options and Exit have black left-aligned lettering; the mode caption is solid
white. Left/right slides between two light-grey PS1 digital controllers and an
extruded Wi-Fi mesh over 22 animation ticks with smoothstep easing. Stock browse
arrows flank the models. Exit/Cancel returns to title; Online Start and the
in-game Options row remain placeholders. The Windows Options > Controllers
entry opens the controller setup panel.

Split screen > Start opens two Pick car panels. Each physical/logical SIO port
controls only its own preview and confirmation. All 32 retail CAR_*.1ST variants
are selectable, independent of GP unlocks, ownership, class or garage progress.
The close-detail body, shell, axle and wheel models and textures are read from
the user's mounted verified disc archive. Two independent texture uploads occupy
VRAM x704..767 and x768..831, y0..255; only car texture pages/CLUTs are relocated.
The stock menu/font atlases remain available. Previews fit within their panels,
rotate, and use a foreground OT layer so the original left panel cannot cover
Player 1's car. Ready locks only that player's choice; Cancel unlocks it, or
returns Player 1 to mode selection. Multiplayer choices never write the GP car
or garage. This stage does not start a split-screen race.

The runtime and prepared portable config now allow two controller ports.
Options > Controllers provides a player dropdown, device assignment, a drawn
PS1 digital pad and individual gamepad mappings. Each seat has an independent
runtime mapping; controllers.ini in the save/settings folder persists devices
and mappings with atomic replacement. Existing keyboard controls remain in use
for a keyboard-assigned seat. One keyboard cannot be assigned to both players.
The runtime's existing controller-claim logic keeps a physical pad exclusive.

Graphics no longer forces 2x when host 3D or PGXP is enabled. A running context
can manually step down to 1x through the retained-resolution API, preserving
VRAM. Higher levels within its allocation can be restored; levels exceeding
the allocated ceiling retain the existing next-launch behaviour.

Validation: all 20 native contracts passed, including independent car choices,
wraparound through all 32 variants, per-player confirmation and cancellation,
all 32 close-detail asset decodes, malformed/truncated asset rejection, menu
hooks for three regions, caption palettes and carousel packet ownership. Live
checks exercised both injected ports, independent remapping/persistence in the
native Controllers panel, actual effective 1x rendering, unchanged GP car,
Cancel-to-mode, Exit-to-title and the 162-check graphics probe. Physical gamepad
hardware was not exercised in this run. Diagnostics and screenshots are in
Port/data/split-screen-development; installation.json records backups/hashes.

### Preview wheels, black picker and artwork export � 23 October 2026

Front wheel translations now multiply the serialized world-space offset by four,
matching SetGteObjectMatrix's translation conversion into mesh units. The original
preview placed them inside the body. All 32 variants have regression checks for
both front-wheel centres and complete wheel geometry. A foreground black canvas
covers the stock course-screen decoration only while the two-player picker is
active; mode selection retains its Time Attack design.

The native picker previously discarded fractional screen coordinates when building
GP0 packets. Its vertices now retain exact 16.16 positions and homogeneous depth
through an address/value-validated host PGXP producer. It neither mutates GTE
registers nor enables the ambiguous position-cache fallback. Tests cover valid
fractions, depth, invalid addresses, mismatches, NaNs, suppression and invalidation.
All 21 native tests and live two-port selection/setup/exit checks pass.

The original Time Attack screen was captured with its texture memory and draw
packets. Menu Graphics/Time Attack contains 23 RGBA sprite fragments, 11 decoded
texture atlases, an element index, reference screenshot and placement manifest;
Time Attack PNG Export.zip contains the complete editing pack. Grid/shading and
animation effects are renderer-generated; the PNGs preserve original source art.

Wobble investigation: the new picker had a confirmed whole-pixel projection step,
now corrected. The original showroom trace has dataflow precision enabled, position
fallback disabled and preserved projection enabled; its sampled mesh vertices carry
fractional coordinates. Source car poses still originate in fixed-point GTE
matrices and integer translations. These findings do not establish the cause of
the reported in-game wobble. Existing older race checkpoints reject the current
runtime identity, so their staged load acknowledgements were not treated as valid
race results. No speculative engine-wide transform change was made. Diagnostics
are under Port/data/car-wobble. The user has clarified that individual panels
and edges bend, rather than the entire car shaking. The homogeneous projection
and retained mesh path were reviewed: mesh-local vertices are preserved and the
GPU applies a shared affine pose, so fractional showroom coordinates alone do
not establish that every reported deformation has been eliminated. A fresh race
probe reached race state and collected native geometry counters, but its race
presentation screenshot did not complete; it is not visual proof of a fix.

### Supplied menu PNGs - 23 October 2026

The seven requested PNGs are retained in assets/menu-options and embedded as a
4-bit luminance-mask atlas. Every palette entry has the blend bit, including
zero intensity. Dark labels use retail subtractive blending (ABR 2); white
captions use additive blending (ABR 1). Black source pixels leave the destination
unchanged and intermediate intensities blend the soft edges into the live menu. Multiplayer uses the supplied mode select,
options, exit, Splitscreen, Online play and vertical MULTI images. Free run replaces
the retail race-start sprite at its scripted position, retaining the slide motion.
The atlas is checked and restored after menu texture uploads; the two car atlases
and user game data remain separate. Live captures are in Port/data/menu-options-art.


Multiplayer carousel placement now centres around logical x=200 within the grey
gradient. The mode captions and arrows move with it (arrow x=124/264), and the
slide clipping bounds move to x=124..278 to retain the button clearance.


Multiplayer mode and both car-picker arrow pairs now have the retail green
background pulse (sine phase step 0x60, byte-wrapped intensity 127..255).
The cable and strain relief move to the rear shell and rear draw layer so the
wire cannot paint over the face. Live mode/Online/picker checks and all 21 native
tests pass. The geometry test also checks all cable faces stay behind the shell.


Title menu refresh: replaced Free Run and Multiplayer normal/selected artwork
with the four updated Menu Button Exports PNGs, retained under assets/menu.
Multiplayer now has two independent indexed rows; its selected row uses the
reserved u112..223/v176..191 area after Press Start and before Free Run.
The retail palettes and title-menu opening/highlight animations are preserved.
The title-menu regression verifies all four reserved rows across PAL/JP/US.


Free-run barriers and pre-race logo: closure filtering uses all four authored
road networks, including arc centres and lane widths, instead of a centre-only
chord corridor. It handles diagonal fork panels while preserving parallel outside
rails, road decks, tall supports and overhead structures. Class-1 fixture geometry
now has 31455 faces / 45410 triangles (previously 31792 / 45858). Tests cover
angled side-of-lane closures, a curved-road gate, outside rails, overhead boards,
64 authored structure samples and six physical junction openings.

The pre-race routine runs again for Free Run, preserving the top Rage Racer logo
and its fades; only its Time Attack sprite and course/timing text submissions are
filtered. DrawSprite/DrawText call sites are mapped for PAL, JP and US; the JP
pre-race entry is corrected to 0x8001c7e0 from the preceding function's return.
All 21 native tests pass.


Barrier material correction: the supplied junction screenshot distinguishes the
red/white closure tile (TPAGE 0x15, U160..191/V96..111) from the black/yellow
divider tiles (U64..95/V96..127). Free Run removes the closure tile independent
of shape and explicitly retains the divider tile before geometric heuristics.
Both the active mesh and every donor use this rule; racing-mode Oval merging
keeps its existing policy. The regression requires no closure tile in the rebuilt
world and checks original chevron triangle samples remain covered. Texture crops
and track-data diagnostics are under Port/data/barrier-materials.


### Closure assemblies and intact side barriers - 24 October 2026

Free Run now omits only identified red/white closure boards and their nearby
metal supports. The former generic road-crossing wall filter is no longer used
for Free Run; ordinary Oval/racing reconstruction keeps its existing policy.
Supports must use the matching metal tile, fit the narrow pole dimensions and
intersect a closure board's height and attachment neighbourhood in aligned world
space. Unrelated poles and all other wall materials remain. Class-1 regression
checks remove 28 active-course support faces, retain 40 chevron surface samples
and restore 60 samples of walls formerly removed by geometry heuristics. The
rebuilt fixture has 31747 faces and 45811 triangles. These are asset-level checks;
the exact player screenshot location has not been reproduced in a live drive.


### Complete junction closures - 24 October 2026

The church/Oval junction also contains silver horizontal rail sections separate
from the red/white boards. Identify that rail tile, then remove it only where
authored, equally elevated road pavement covers samples on both sides. Road
coverage uses the original line/arc contacts; exterior rails fail the outward
sample. Narrow metal supports are matched against the full edges of cleared
boards and rails, rather than just their corners. No generic wall deletion is
reintroduced. Class-1 checks remove 20 road-closing rail faces and 136 attached
support faces, preserve all 40 chevron samples, and verify no classified closure
rail/support remains in the rebuilt world. All 21 tests pass.

Live QA reached the church junction and viewed the flat Oval entrance and the
uphill divider separately: Port/data/closure-live/entrance/junction.png shows the
open entrance, and fixed/junction-verified.png shows the intact uphill divider.
Debug write_ram accepts a bounded, validated hex RAM block for coherent diagnostic
pose placement; the original one-byte val form is retained. This does not alter
the normal game flow.


### Tunnel closures and partial junction contact - 24 October 2026

Free Run removes the authored tunnel blackout/rim tile (TPAGE 0x16,
U64..95/V16..47) from active and donor meshes. Other tunnel walls, ceilings
and yellow/black dividers remain. A live tunnel-mouth capture confirms the
opaque cap is absent: Port/data/tunnel-caps-collision/live/opposite.png.

Junction collision now checks each rotated hull corner against the union of
same-elevation authored roads. Corners supported by a connecting route no
longer constrain the selected road edge; unsupported corners retain exterior
wall contact. This fixes the previous whole-hull requirement that left partial
invisible collisions while entering a branch. Regression covers a car straddling
an open connection and an unsupported outer edge. All 21 tests pass; the Release
build succeeds. Live cap visibility was checked; a complete driving sweep of
all junctions has not been performed.

### Free Run drive sweep, local tunnel approaches and TV - 24 October 2026

Completed eight input-driven laps: BIG, MID, HI and Oval in each direction,
changing networks between laps and turning the car around for the reverse
passes. Later passes included gentle lane weaving. No car-position RAM writes
were used during the eight-lap sweep. Debug turbo accelerated parts of the
sweep; paced presentation was restored for its screenshots. Summed driving
wall time was 1882.5 seconds. Isolated saves and dummy audio were used. Records:
Port/data/free-run-drive/eight-laps.json and live/drive.jsonl. A separate pose
replay was used only for focused before/after checks, not counted as laps.

Confirmed HI first tunnel exit pop-in at point 46: baseline screenshot
live/r2-d1-t0048.png shows sky beyond the tunnel instead of the approach road.
The merged mesh contains that road, but its upper-section visibility flag was
hidden by the current route-index gate. Nearby later-section sectors now remain
available within 8192 horizontal / 2048 vertical world units of either camera
endpoint. The distant authored backdrops retain their original concealment;
revealing the entire upper world exposed inappropriate distant geometry and
was rejected. GPU depth/frustum clipping still applies. Regression covers the
actual approach face and a distant overhead backdrop. Final capture is under
verified-release/tunnel-final.png.

Collision write traces found unnecessary one-to-four-unit corrections at
HI (22056,5126,23012), stadium (49958,6000,11760), and MID
(39929,2549,40733), despite full same-height pavement support at every hull
corner. A fully supported hull now bypasses only the single-ring edge clamp
for that contact call. Height, pitch, banking and surface response still run;
an unsupported corner restores normal boundary contact. Partial junction
limits also publish the correct retained corner contact IDs. Identical short
pose/input A/B replays reproduce old corrections at all three positions and
none in the final build: collision-recheck-final.json. A separate unsupported
outside-wall control still applies a 169-unit correction. Geometry/height
sampling of the eight driven laps found no sampled ground mismatch over 96
world units; this is not exhaustive coverage of every driving line.

The canopy TV uses its original models 24..39 and four-tick normal loop in
Free Run, with message model 7 bound to the supplied native 112x32 freerun.png.
All five supplied colours match the original message palette exactly: packing
is four-bit with no resampling. Retail palette cycling is retained. The Grand
Prix draw guard is borrowed only within the screen routine and restored before
model submission or on early exit; no race mode is permanently changed. The
original message pixels are restored when Free Run racing ends. Regional tests
cover PAL/JP/US guard restoration, adjacent halfword preservation, early exits,
model selection and pixel restoration. Live TV capture: verified-release/tv-final.png.
Source asset: Src/assets/tv/freerun.png; packed image: rage_free_run_tv_art.h.

Original TV art is exported under TV Animation Export/Original Pixels, including
18 unique native-size texture crops and original model positions/sequences.
The initial enlarged composites were moved to Enlarged Reference Previews and
are explicitly marked as previews. They are not native-size originals.

Final Release build and all 21 tests pass. Eight laps were baseline diagnostic
coverage; final validation consists of the focused A/B collision, outside-wall,
HI tunnel visibility and TV checks plus the regression suite, not eight more
full laps of the changed build. No GitHub release was requested this turn.

### Tunnel-only speedometer lighting - 24 October 2026

DrawPlayerTachometer originally selected dark/fade modes using both zoneDark
and g_EnvScriptClock. Free Run route/progress changes move that original clock,
so the dial could switch outdoors. The DrawTachometer entry now selects mode 2
only when the player's physical position passes the existing sustained-tunnel
and direct-roof checks, otherwise mode 0; blend amount is zero. This applies to
active races in all driving modes and leaves non-race presentation untouched.
The stable rendered-height field is used, rather than contact scratch height.
PAL/JP/US argument regressions cover all four incoming lighting modes, both
tunnel states, unchanged RPM/shift-light values and non-race preservation.
Live outside/tunnel/outside captures under Port/data/speedo-tunnel/live confirm
normal / illuminated dark / normal appearance. Release build and 21 tests pass.


### Free Run held-jump trap - 24 October 2026

Higher-speed, wider-line driving reproduced an actual below-road/stuck car on
OVAL5, not missing render geometry. At raw (55442,5822,1403), speed 899, the
player entered vertical state 2 and retained launch Y=5820. The bank then rose
to Y=5667 at (55708,4240): the physical car was 153 game units beneath its
correctly computed road contact. It eventually stopped while still in state 2.
Telemetry and captures: Port/data/free-run-ground/live; diagnosis.json records
the launch and deepest sample. Position logs use modelY as the stable floor;
the primary Y word can contain contact work during a debug read.

GetCarCrestTrigger still read the starting circuit's event table while Free Run
rescaled progress across BIG/MID/HI/OVAL. Its negative event at progress 56071
holds the car at its launch height until the authored course drops away.
Crossing it on an unrelated bank can instead put the car below pavement and
leave the airborne drivetrain unable to accelerate. The higher-speed threshold
explains why earlier low-speed laps did not reproduce it. A different line in
a repeat baseline lap crossed the same event and landed normally, confirming
that merely crossing the point is insufficient to reproduce the trap.

For the Free Run player only, GetCarCrestTrigger now returns no scripted launch.
These start-course scripted hops have no valid coordinates in the combined
world; normal road contact, banking, outside walls and airborne/landing routines
remain intact. Grand Prix, other cars and non-race scenes retain retail lookup.
No road meshes or floor heights were changed for this fix. Entry points were
verified against all three generated 252-byte functions: PAL 39184, JP 38B6C,
US 38EF0. Regression covers regional mapping and mode/player/scene scope. Release
build and all 21 tests pass. Driving coverage is recorded separately in
Port/data/free-run-ground/candidate/wide-summary.json and drive.jsonl.

The fixed build then completed eight full input-driven laps, all four networks
in both directions, weaving toward both sides (target amplitude 80% of local
half-width) with a target speed cap of 1150. All eight completed, 10,031 position
samples recorded, no held-jump launches, no sustained below-road/stuck failures.
One isolated debug read showed physical Y=4128 and old modelY=4104; the actual
HI surface at (17076,24071) is Y=4128.91, so this was a read between contact and
model-height copy, not penetration. Two large per-sample floor changes were on
the authored steep hill; their rise/run was continuous across adjacent samples.
These are the tested driving lines, not exhaustive coverage of every possible
position. Final summary/review: free-run-ground/validation.json. Three installed
launchers share SHA256 4C2F468E95B7009B2367AFCDF56F43DAE28119309601A5CFB6A53C5C9230A04A.
Installation receipt confirms game data, settings and memory cards unchanged;
old executables are backed up. Diagnostic games used isolated saves and have
been stopped. No GitHub release was requested for this change.


### Sky-driven lighting and local environment shadows - 24 October 2026

Ported RenderDirectionalLightFromSky from the reference render_world.c into
rage_environment_light.h. The displayed top/horizon palette determines the
ambient hue, directional colour and elevation; azimuth remains world-fixed.
The packed environment ABI is respected (slots begin two bytes into the block,
12 bytes per slot). Top/horizon byte loads were checked in generated PAL, JP
and US sky functions: block bases 801E3FB4 / 801E3744 / 801E3CBC. A race-only,
immutable tick snapshot interpolates the bands for generated frames; the GPU
never reads guest memory. Menus and sky/HUD rendering retain their own shading.

The existing 4096-square texel-snapped shadow map now shares the sky-derived
sun direction. Track, cars, scenery and road decals receive the lighting and
shadows; original vehicle shadow plates remain replaced when native shadows
are available. Local lamps and emissive lenses remain additive. The modulation
is blended with the authored texture/vertex shading to retain the original art.

Nearby opaque/masked terrain is separately submitted to the light frustum,
including geometry behind the viewing camera. It uses the retained terrain
identities and exact same resident payloads as the colour pass. Shadow-only
receipts are excluded from colour/depth rendering, and dropped safely if the
retained path is unavailable. Original Free Run later-layer concealment still
applies. Decals, sky and translucent texels do not become opaque casters. Wall
winding and scenery appearance dithering do not change light occlusion. A
padding margin covers camera/player anchoring and interpolation. A soft map
edge fade avoids a moving hard rectangle at the local shadow-volume boundary.

Resident GPU copies gather compatible environment ranges into a few light
draws rather than one draw per sector. Main/mirror transforms and colour draw
ordering stay independent; original scene-depth initialization happens before
light receipts are queued, so its clear cannot drain the new shadow map early.
The 16384-vertex-unit extent and 4096 resolution reuse the existing allocation;
this is bounded local raster shadow mapping, not a global-illumination/ray
tracing port. Debug controls RAGE_SKY_LIGHTING=0 and RAGE_ENVIRONMENT_SHADOWS=0
allow comparisons. Existing RAGE_VEHICLE_SHADOWS remains the master map toggle.
frame_gen now reports environment map/caster/lit-group counters separately.

Validation: all 21 contract tests pass, including sky hue/elevation, valid night
ambient, dynamic light map projection, off-camera caster inclusion and distant
caster rejection. The actual-driver material probe adds environment depth,
transparent cutouts, warm/cool fragment tint, and map-edge fade controls (168
samples). Driving/visual diagnostics and profiles are under
Port/data/sky-lighting. Two Free Run laps covered BIG in both directions; one
Grand Prix lap covered the normal course with rivals active. These broad runs
preceded the last batching/decal/edge-fade refinements; subsequent fixed-pose
checks and final GPU/rival checks validate the final renderer separately.

Three equal-palette, isolated-process comparisons used stadium, climb and
waterfront chase views at 2x. Before the final inexpensive edge-fade expression,
median real-frame CPU overhead was 0.84--1.24 ms, generated GPU overhead
0.89--1.33 ms; generated CPU time was essentially unchanged (-0.049 to +0.023
ms). No frame-generation performance cutoffs occurred in those samples. These
are local measurements, not an all-hardware frame-rate guarantee. Detailed
before/after medians and p95 values are saved in baseline/profile.json,
final/profile.json and performance-comparison.json. Existing Free Run distant
upper-backdrop artefacts are visible in both comparison captures and were not
created or modified by this lighting change. Collision/road data is untouched.

The final edge-fade build passed all 168 actual-driver probe samples. A focused
Grand Prix check with a player pose staged near an active rival recorded 565
vehicle caster draws, 122 environment caster draws and 1519 lit groups over 49
real / 71 generated frames, with no additional performance cutoffs. This was a
rendering control, separate from the earlier naturally input-driven GP lap.
Final executable SHA256: 2F1E12F3E9A118A1F4890814F6C481787B896D26F243A9FE0852E78402099DAB.
All three launchers match this tested binary; installation.json verifies game
data, graphics/controller settings and both memory cards were unchanged. Old
launchers are backed up and own diagnostic processes are stopped. No GitHub
push or release was requested for this renderer change.


### 2026-10-24 — sky lighting UI isolation, brightness and stability

- Preserve RAGE_SCENE_UI through the generic vertex key (bit32) into native
  attributes (bit32768). UI fragments return their authored colour before any
  directional lighting, shadow attenuation, headlights or lens emission, and
  are discarded from shadow depth rendering. Road decals remain world lit.
- Increase sky-derived ambient from .18+.20*day to .26+.30*day and direct
  intensity from .52+.34*day to .65+.45*day, retaining horizon tint and elevation.
- Compute surface-normal derivatives from camera-relative world positions,
  before adding the distant world translation. Always anchor the shadow grid
  to the camera instead of switching between camera and player by queue
  contents; apply slope-scaled polygon offset (2,4) during shadow depth only.
  These address precision noise, inconsistent grid placement and self-shadow
  acne. No claim that every possible reported building has been reproduced.
- Validation: all 21 CTest targets passed; UI packing test subsequently added
  and native_geometry_contract passed again. Actual-driver material probe:
  168 reported samples, zero failures, including additional warm/cool UI colour
  invariance checks. Three live scene profiles had zero performance cutoffs.
  Actual controller-driven Free Run route0 reverse lap completed in 71s.
  Twelve stationary stadium captures: centre/right building regions were
  pixel-identical; left region maximum channel spread6 with no pixels >8.
  Captures, profiles, lap log and installation receipt are in
  Port/data/sky-lighting/brightness-fix/.
- Installed all three Shareable launchers with SHA256
  F42CBABCAD1B794CEF678688BB1CA670BA267557F2602EAD188FCB658DC22991.
  User data.dat, settings and memory-card hashes verified unchanged; previous
  launchers backed up as recorded in install-before.json. QA process stopped.


### 2026-10-24 — quick pre-race confirmation and configurable distant shadows

- Reproduced quick Free Run confirmation with real pad input: scene12 kept
  updating/drawing with GPU display disabled (gpu_state.disabled=1), so audio
  continued but no presented screenshot completed. The music hook accelerated
  the ROUND timer to120 before stock setup1/display-enable15 could execute.
  rage_music_round_timer now preserves those setup events before fast-forward.
  Early confirmation remains latched. Regression checks every confirm frame0..15.
- Start and the mapped accelerator both confirm songs. Schedule the existing
  ROUND cue call at32 once, substitute the standard menu confirmation cue2,
  then permit fast-forward. Guest stack/calls/audio bank remain stock. Verified
  PAL live function traces show exactly one cue2 from ROUND return8001D03C
  for Start and accelerator, and display enabled on both quick-start runs.
  Cue entry/return mappings verified against PAL/JP/US generated sources.
- Add persisted Graphics > Use sky as a light source (sky_light). Off disables
  the added directional tint and real-time shadow passes, retaining authored
  colours, original car shadow fallback and existing car lamps. Restore original
  settings also switches sky lighting off. Existing preferences default on.
- Add persisted Graphics > Shadow quality: Off/Low/Medium/High
  (shadow_quality=0..3, defaultMedium). Live menu choices use no restart.
  Low2048, Medium4096, High8192 depth maps, clamped to GL_MAX_TEXTURE_SIZE.
  High uses four bilinear comparison footprints (16 depth comparisons per
  cascade) for smoother moving edges; other levels use four comparisons.
  Recreate depth targets when quality changes; qualityOff disables all native
  shadow casting/receiving while leaving the sky's colour lighting available.
- Add a distant cascade spanning1.5*the current geometry far limit, alongside
  the detailed16384-unit near cascade. Blend overlapping grids and scale far
  receiver bias in world units. Geometry selection uses the distant light
  volume independently of viewing direction; standard131072 and extended196608
  draw distances are covered, including reverse views. Texel snapping uses
  the selected map resolution. Actual-driver regression verifies an occluder
  still shadows a receiver outside the near map, including the high filter.
- Shadow caster expansion exposed cache/queue pressure:512 mesh entries and
  a retained-terrain1024-record flush could split the caster list. Raise mesh
  entries2048, retained lookup8192, scene queue4096 consistently, retaining
  the32MiB source payload limit. This avoids incomplete shadow-list rebuilding
  in the examined views and reduces repeated retained geometry rebuilds.
- Validation:21 CTest targets passed, including timer setup and map-range/
  resolution regressions. Production GPU material probe reports168 samples,
  zero failures, including additional far/high/UI cases. Full controller-driven
  Free Run route0 reverse lap completed with the two-cascade renderer. Three
  High scene profiles show no performance cutoffs. Building-edge shimmer is
  not claimed eliminated in every scene; source fixes address partial lists
  and High adds resolution/filtering. Diagnostics are in
  Port/data/free-run-transition/, including live menu/INI checks, sound traces,
  screenshots, scene profiles and controller lap logs.

Final quality installation: all three launchers SHA256 DAD09C3002A702965381EA223B76C6A273523E7A96DD08572F5398832879ED78.
Live Win32 submenu checks verified Off/Low/Medium/High labels, checkmarks,
saved values and immediate shadow-pass changes. GPU probe passed after each
choice. User data/settings/cards hashes unchanged; backup path and receipts
in Port/data/free-run-transition/quality-installation.json.


### 2026-10-24 — reproduced dome/finish shadow artifacts

- Reproduced against installed DAD09C3002A702965381EA223B76C6A273523E7A96DD08572F5398832879ED78
  with two independent game instances, matching car/camera poses near the finish.
  The glass dome showed a large dark outer patch; repeated frames also caught
  the finish banner changing between normal and heavily shadowed texture states.
- Remove automatic promotion of arbitrary non-terrain 3D groups into scenery
  shadow casters. A shared viewport does not prove a world transform. World
  lighting now requires terrain or explicit vehicle tags; camera-space effects
  also bypass local light spill/lens emissions. Translucent terrain groups
  neither cast opaque environmental shadows nor receive projected shadows.
- Correct receiver bias: far-map tolerance was scaled down by near/far extent,
  holding it to a few world units despite much larger far-map texels. Both maps
  now use4096/actual_resolution bias scaling in their own depth coordinates.
  Actual GPU control reproduces false occlusion at the former small bias and
  verifies the corrected tolerance while retaining true distant occlusion.
- Conservative receiving policy: opaque scenery faces with abs(normal.y)>.55
  (roads, banks, ground and similar surfaces) and all vehicle panels receive
  projected shadows. Upright texture artwork/walls retain authored shadow
  detail and sky colour lighting. This intentionally forgoes dynamic projected
  wall shadows to avoid unstable self-overlap in these two-sided retail meshes.
  Opaque scenery still casts onto roads/cars. Dome glass uses its original
  translucent presentation, without opaque self-shadowing.
- Validation:21 CTest targets passed, including new world/receiver classification
  checks. GPU probes passed after Off/Low/Medium/High live switches, including
  upright-sheet, vehicle receiver, distant-occluder and old/new bias controls.
  Matched16-frame captures: verified dome, finish banner and tower crops were
  pixel-identical across all captures. The concurrent old build had respectively
  11017,18976,652 pixels varying by more than8 channel levels. Source captures
  and exact boxes/results are in Port/data/shadow-regression/verified-stability.json.
  A genuine controller-driven Free Run route0 reverse lap completed on High.
- Installed all three launchers SHA256
  251E41F61E3E5F71D2066091385145DD067F95BE00DE4410679DAAA198E1D4B8.
  Previous launchers backed up; data.dat, user graphics/controller preferences
  and memory-card hashes unchanged. See shadow-regression/installation.json.


### 2026-10-24 — preserve scene-frame shadow maps across queue flushes

- Found a map lifetime bug: each native queue flush rebuilt/cleared both maps,
  including terrain-only receiver flushes (the previous fallback selected a
  terrain group even with no actual casters). Material updates/cache pressure
  could split the caster list, replacing a complete map with an empty/partial
  one later in the frame and briefly removing the shadows.
- Remove the no-caster terrain fallback. Map lifetime follows the existing
  scene-depth frame reset. The first actual caster batch clears/builds both
  maps; subsequent caster batches append depth using the same near/far rows.
  Receiver-only batches reuse the existing maps. Freeze sky-light uniforms to
  that frame's map direction/colour, including batches without a sky receipt.
  Quality changes still recreate/clear targets; sky/shadow Off still bypasses.
- Add an explicit update-plan helper and CPU regression for caster/receiver/
  later-caster sequencing. Actual GPU regression verifies that a receiver-only
  flush preserves depth and a subsequent farther caster cannot erase the first.
  frame_gen exposes shadow_frame_clears/appends/reuses for verification.
- Diagnostic RAGE_NATIVE_QUEUE_LIMIT (128..4096, default4096) forces more queue
  boundaries without changing shipped defaults or increasing memory limits.
  Tested the same binary in normal4096 and stress128 configurations. Live
  counters recorded11462 append and11114 receiver-only reuse operations in
  the stress capture run, and210/5126 respectively in the normal run, proving
  these paths occur even without artificial pressure.
- Validation:21 CTest targets passed.40 consecutive captures per configuration
  had pixel-identical road-shadow, finish-banner and dome crops (zero channel
  variation); exact boxes/counters are in Port/data/shadow-lifetime/stability.json.
  A controller-driven Free Run route0 reverse lap completed in61.41s on High.
  Live Off/Low/Medium/High menu switches saved correctly and GPU probes passed
  after each. Captures, lap log, probe/menu evidence and receipts are in
  Port/data/shadow-lifetime/.
- Installed all three launchers with SHA256
  E1E7A840A4B646021160D825A45A6C34B492C29A6EA6B025DD2655840320F451.
  Previous launchers backed up; data.dat, real user graphics/controller settings
  and memory-card hashes unchanged. See shadow-lifetime/installation.json.


## 2026-10-24: eliminate redundant retained terrain work

- Keep the current OpenGL backend and all visual quality settings. The retained
  terrain cache now uses four-way sets with LRU replacement (same 8192 entries),
  avoiding direct-mapped collisions. Validate the actual referenced material
  slot/key identities instead of invalidating all geometry on unrelated atlas
  layout changes. Dirty texture pixels still refresh through material_acquire
  with its existing ordered flushes; texture animation does not rebuild vertices.
- Reuse the GPU terrain gather buffer only when the complete ordered immutable
  allocation serial, buffer handle and vertex ranges match. Live transforms,
  lights, fade and viewport continue to update normally. Changed layouts still
  orphan/copy, errors invalidate reuse, release clears the cached layout.
  Added frame_gen reuse/saved-byte counters and stream identity CPU regressions.
- Matched three-scene scale2/High-shadow profiling eliminated warm terrain
  rebuilds; intermediate-frame CPU medians improved 25-36%. Then repeated at
  the actual saved scale6/120fps/16xAF/High-shadow settings, using isolated saves.
  Stadium real CPU 3.840->2.988ms, climb 3.540->2.893ms; intermediate CPU
  0.786->0.526 and 0.579->0.456ms. Waterfront first short run had worse real
  CPU/GPU timings despite lower intermediate cost. A longer reverse-order
  repeat measured CPU 3.816->3.616ms and intermediate CPU 1.116->0.662ms;
  real GPU 6.673->7.031ms remained worse, intermediate GPU 2.867->2.182ms.
  These are sampled costs, not an across-the-board FPS guarantee.
- At saved settings, all warm terrain rebuild counts became zero; GPU copy
  traffic fell about87-96% over comparable capture windows. Sky palettes and
  staged before/after visuals match; no frame-generator breaker trips occurred.
  Measurements/screenshots: Port/data/performance-pass/, including original
  short runs and longer waterfront repeats. No quality settings reduced.
- Validation:21/21 CTest targets passed; final-binary GPU material/shadow probe
  passed168 samples, zero failures. GP startup and live counters exercised
  vehicles, shadows and retained terrain. Controller-driven Free Run route0
  reverse lap completed with the optimized candidate. Finish/dome captures
  were inspected on final binary. This pass did not retest every driving route.
- Installed all three Shareable launchers; SHA256
  3416AE2662C2CF9C810946FD72E24909D04E798CEF286DAB3D249A8E2B001FEB.
  Backed up previous launchers. Game archive, real graphics/controller settings
  and both memory cards verified unchanged. Receipt:performance-pass/installation.json.


## 2026-10-24: live rendering resolution selection

- Replace the graphics menu startup-allocation ceiling restriction with a
  synchronized manual scale request. Initialize resize resources even when
  starting at1x; raise the manual ceiling on demand within the full-VRAM
  GPU dimension/memory budget and current wide-surface limits. Resize occurs
  through the existing ordered post-present path preserving VRAM/display pixels.
  No maximum-size allocation at startup; no context/window/game restart.
- Remove the restart-required menu label. Saved resolution still persists.
  Automatic dynamic-resolution ceiling behavior remains separate. Dual-raster
  and high-resolution-window regimes retain their existing resize restrictions.
- Live test started at1x and selected2,4,8,1,6,3,1,8,2 through actual WM_COMMAND
  menu dispatch. Each reported the matching real framebuffer size and passed
  168 GPU probe samples with zero failures. Screenshots confirmed visual scale
  changes. All six menu scales exercised without restarting the process.
- VRAM preservation probe stepped1,2,3,4,6,8,1,8,2:zero hash mismatches, restored2x.
  QA settings and cards were isolated; user preferences/archive/cards unchanged.
  Installed all three launchers with SHA256 F52DB669BDD201B9AFAB17E0DBA86A58FB6099ACF185D8CC9813CA9EB842D5D0.
  Evidence/backups receipt:Port/data/live-resolution/installation.json.


## 2026-10-24: tunnel fixtures, shadow ambient and Free Run route ownership

- Infer fixed tunnel fixture positions from bright yellow texels mapped through
  authored UV triangles, restricted to small surfaces around sustained roofed
  road sections. Deduplicate close samples and keep route ownership. Captured
  player-light snapshots now carry up to16 nearby fixture positions, so the GPU
  does not read mutable track state. Loaded Free Run union found640 samples;
  GP course found76. These are inferred light patches, not separately authored
  lamp objects. No asset modifications or per-fixture shadow maps were added.
- Local warm lights attenuate by distance/orientation below each fixture, replace
  sky-driven shading inside their influence, and preserve authored texture colour.
  UI keeps its early lighting bypass. Sky lighting Off bypasses these additions.
  Add .08 to sky ambient; directional shadows retain the full ambient term and
  16% diffuse floor instead of darkening ambient to55% and diffuse to10%.
- Tag combined-world faces with route ownership bits. Exact duplicate and
  coplanar shared surfaces inherit donor ownership during union construction;
  metadata-only merges are retained. Prepared batches split at route/layer
  boundaries with individual bounds and stable identities. Both colour and
  off-camera shadow submission use the same route/bounds filter. Shared/current
  route geometry stays; nearby connectors from other routes remain available,
  while distant/vertically separated alternate-route batches are hidden and
  excluded from shadow submission. Legacy colour path uses the same policy.
- CPU regression validates all four exclusive ownership sets and shared surfaces
  (2699/9626/8848/3964 exclusive,6220 shared), connector visibility and stacked
  route rejection.21/21 CTest targets passed. Final GPU material/shadow probe
  passed in Free Run and GP, including updated ambient expectation and a new
  warm/local/below-fixture test. Existing UI, mirror and shadow lifetime tests pass.
- Controller-driven route0 reverse and route1 forward laps completed. Staged
  captures also cover route2, both route0 tunnels and a GP tunnel. Not every
  route/direction was driven in this pass. At scale6/120fps/High/16xAF, a short
  tunnel sample had median realCPU2.170ms, realGPU7.787ms, genCPU2.285ms,
  genGPU1.941ms, zero breaker trips. This is a spot check, not a full benchmark.
- Installed all three launchers, SHA256 53ED829B083B1BCB5CD6ACE18D166EF3F8A0F90017C7B7B3DE21B1210B63AA19.
  Archive, real settings/controllers and both memory-card hashes unchanged.
  Captures, driving logs, GPU probes and installation receipt:
  Port/data/tunnel-route-lighting/. Previous launchers backed up.


## 2026-10-24: Extreme Oval route-filter exemption

- Route bit3 (Extreme Oval), including shared faces, bypasses route and upper/
  lower-section hiding in the common colour/shadow visibility helper. Normal
  frustum and draw distance still apply; other routes retain filtering.
- Exclusive/shared Oval regression with distant vertically separated bounds
  passed in free_world_geometry_and_routes; release build succeeded.
- Installed: RageRacer-Updated.exe, RageRacer-MultiplayerTest.exe. Running/locked: RageRacer.exe.
  Also staged RageRacer-OvalUpdate.exe. SHA256 AD51FB4BEC886FFB45F13991B27CDC7DBB5634262A141C2333BFC11AE9E5D8B9.
  Protected data/settings/cards unchanged; receipt:Port/data/oval-filter-exemption/installation.json.


## 2026-10-24: tunnel route windows, original tunnel shading, fast Free Run entry, stats

- Replace proximity-driven alternate-route appearance with two authored tunnel
  portal windows per main route. Both travel directions use the same intervals;
  one road segment of portal overlap covers the camera/car crossing. All three
  main routes open inside either window; outside, only chosen/shared geometry
  survives the common colour/shadow filter. Extreme Oval remains always exempt.
  The combined world/CPU-prepared geometry stays resident rather than reloading
  from disc per branch. Ordinary frustum and draw-distance clipping still apply.
- Remove the yellow-fixture extractor, immutable fixture snapshots, GPU uniforms,
  local warm lighting shader and fixture GPU probe. Tunnels retain the original
  authored textures and existing car lamps. Outdoor ambient improvement remains.
- On Free Run scene12, suppress the old ROUND artwork (finish its fade level)
  while retaining the stock game fullscreen fade. Begin that fade immediately
  rather than spending its first six frames fully black. Keep ROUND setup1,
  display15, asset readiness, song confirmation and the existing chime intact.
- After a confirmed Free Run song/chime, use the existing turbo-through-loads
  path in scenes10/11 only, without changing user speed preferences. A120ms
  chime lead-in preserves feedback; accelerated output uses the existing audio
  sink, and normal pacing resumes on scene12. Required asset/mesh initialization
  still takes time. Matched diagnostic traces from first observed ROUND to race:
  9.80s before loader acceleration,6.44s final. This is not an instant-load claim.
- Graphics > Display stats toggles immediately and persists display_stats in
  graphics-options.ini (defaultOff). White top-right host OSD on translucent
  background; unaffected by world lighting/internal resolution. FPS counts actual
  swaps, including generated frames. Frame time is the sampled mean presented
  interval. Cache is the native mesh source cache, currentMiB/32MiB budget, via
  an atomic occupancy receipt (not cumulative uploads). CPU is game process
  usage normalized over host logical processors. GPU uses Windows PDH's busiest
  game3D engine, N/A when unavailable. Poll/rasterize at2Hz; disabled has no
  polling or drawing. Separate OSD texture avoids reallocating the minimap texture
  every frame. No new external runtime dependency; PDH/GDI are Windows libraries.
- Validation:21/21 CTest targets pass, including all three regional Free Run fade
  regressions and all three routes/both tunnel windows in both directions. Actual
  controller passes crossed the first tunnel forwards (point36->138) and last
  backwards (190->127), with no below-road failure. Final GPU probe168 samples,
  zero failures. Live stats toggle/persistence and1x/6x/2x switches passed; actual
  CPU/GPU values and final OSD visually verified. Menu/game captures, transition
  traces and driving logs are in Port/data/portal-stats/.
- Installed launchers: RageRacer.exe, RageRacer-Updated.exe, RageRacer-MultiplayerTest.exe, RageRacer-OvalUpdate.exe. Running/locked: .
  SHA256 C6D0339D4A24C8AF53D0398B37DBB35D219C28BFBD31882E8E27D964BC16D7EA.
  Real archive/settings/controller/card hashes unchanged; prior executables
  backed up. Receipt:Port/data/portal-stats/installation.json.

## 24 October: draw distance, route crests and race fade

Normal terrain/environment depth is 106496 (original81920, previous131072); extended depth139264. Shadow extent shares the terrain limit. Route-local chunk9 crest events restore positive hops and negative crest holds in both directions without applying the starting route's events to another road. All28 actual-disc authored events pass. The original race draw and fullscreen fade run normally, with only hidden race artwork fade shortened. Confirmed Free Run loading temporarily uses bounded4x data-disc timing and restores the prior rate on gameplay entry; an instant-disc experiment was rejected and is absent. Recorded transition7.99s ->5.82s; required initialization remains.21/21 tests, full driving lap, positive rising jump, and168/0 GPU probe pass. Evidence:Port/data/distance-fade-jumps/bounded,drive,positive-close.json. Earlier final/ directory is a rejected experiment, not release validation.

## 24 October: persistent Free Run portal aprons (v0.3.30)

Route switching already selects draw batches without releasing drawing_course or drawing_prepared. Race initialization alone decodes/merges donors and prepares immutable batches. Keep that lifecycle; no route-triggered loading, eviction or rebuild is introduced. The renderer's ordinary bounded material/mesh caches remain in use.

Bake free_run_portal metadata from path distance between the first tunnel's exit and last tunnel's entrance. Nominal 20m=640 authored road units; rendering uses four times the road scale. Include faces whose horizontal bounds overlap either distance interval, rather than physically slicing polygons. Preserve the flag through exact and coplanar donor merges. Separate prepared batches by portal flag and apply the shared predicate to scene, mirror, fallback and shadow-only draws. The middle of inactive branches still hides, and Extreme Oval remains exempt.

Validation:21/21 native tests; actual-disc world contains persistent portal faces and filtered interior faces. Boundary and overlap tests plus all three route ownership masks pass. Live 168-sample GPU probe has zero failures. Diagnostics:Port/data/portal-aprons/.

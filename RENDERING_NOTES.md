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

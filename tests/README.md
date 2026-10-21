# Native rendering contracts

Build and run the small standalone test project:

```powershell
cmake -S Src/tests -B Port/build/native-validation
cmake --build Port/build/native-validation --config Release
ctest --test-dir Port/build/native-validation -C Release --output-on-failure
```

The tests cover mesh hashing at unaligned addresses and partial-word tails,
camera-independent mesh identity, and shared filtering bounds for connected
texture fragments. A UV seam, different pose/material, or a repeated corner
must not join separate surfaces. Filtering-bound repair preserves positions,
UV coordinates, colours and material properties.

The renderer also retains exact byte comparisons on every mesh-cache hit,
including replay-order hints. Hash collisions and stale hints cannot select
different geometry.

The indexed geometry contract reconstructs every original vertex byte, including
UV, colour, material, pose, signed zero and NaN payload differences. Unique or
unprofitable meshes retain expanded rendering. It also compares 131,072 direct
terrain vertex payloads against the former staging/packing path, including
all signed depth biases, reflected wall winding and filtering/window options.
The OpenGL material probe additionally compares an expanded and indexed draw
over all 65,536 pixels of a cleared test region on the actual driver.

The starter clipping scope test verifies exact restoration of all four model
reject bounds and preservation of neighbouring scratchpad state. The driver
probe additionally verifies geometry inside the new one-unit near plane is
visible at depth 2, and geometry at depth 0.5 is rejected, while preserving the
existing terrain and close wheel/body depth-occlusion tests.

Vehicle contracts cover the reference's directional light, snapped shadow camera,
main/mirror inverse views, ground constraint (including valid airborne motion),
and world-space wheel attachment while a camera orbits through 90 degrees.
The native driver probe adds caster-only depth writes and shadow/lit/bias
reception checks. Captured vehicle paint remains authored; shadows are received
by world geometry rather than the retail car panels.

Wheel stability coverage also distinguishes rival front/rear wheel streams,
player wheel/blur banks, shadow plates and far body meshes. Shadow-centre tests
change arbitrary non-player palette transforms while requiring the camera
fallback to remain fixed; an explicitly tagged player pose takes priority.

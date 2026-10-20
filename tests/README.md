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

# LumaText Release SDK update — 2026-09-22

Updated the bundled Release SDK from `../lumatext/out/sdk/Release` to the optimized x64 `/MT` build. The DLL is 1,648,128 bytes; SHA-256:

`1EAE7113B138FB26C9D47234D954CB53DABFD1FE0824EB10B41F831781D10535`

The matching import library is synchronized. Header/license contents already matched and were preserved. `SHA256SUMS` was regenerated and every listed file verified. Existing Debug DLL, PDB, import library and CMake configuration mappings were preserved byte-for-byte. Existing `src/core/lumatext_bridge.cpp` changes were not edited.

Validation:

- Built `lumen`, `lumen_gallery`, `lumen_visual_test`, `lumen_perf_test`, `lumen_anim_test`, and `lumen_api_test`; API naming check passed.
- Visual and animation executables: `ALL PASS`; API executable: exit 0.
- Inspected the generated dark visual state board; text and controls render correctly in this fixture.
- Performance budget: average 5.593 ms/frame over 300 frames, worst 28.855 ms; mean remains below the required 8 ms. A separate ShowBox compilation was running, so these timings are not a controlled before/after performance comparison.
- `build/lumatext.dll` matches the source SDK and bundled DLL SHA-256.

Build/test logs: `build/lumatext-optimized-update-build.log`, `build/lumatext-optimized-visual.log`, `build/lumatext-optimized-anim.log`, `build/lumatext-optimized-api.log`, and `build/lumatext-optimized-perf.log`.

No installed application was replaced. Manual Gallery interaction, physical DPI/display switching and AutoCAD host behavior were not tested by this project update.
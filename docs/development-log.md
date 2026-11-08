# Development log

How LumaCore was built: milestone by milestone, with the problems found on the way and how
each was resolved. Every milestone ended only when its acceptance criteria (in
[plan.md](plan.md)) were met: all tests green, no compiler warnings, results checked.

## Summary

| Milestone | Delivered | Verified by |
|---|---|---|
| M1 Skeleton | CMake + presets, strict warnings (`/W4 /WX /permissive-`), GoogleTest via FetchContent, conventions in CONTRIBUTING.md | clean configure/build/test |
| M2 Geometry | `Vec3`, `Ray`, plane/sphere/triangle intersection, box meshes; geometry demo image | 56 tests incl. randomized properties |
| M3 Lighting | E = I cos θ / r², scene, measurement grid, statistics (U0), CLI → CSV → isolux heatmap | closed forms to 1e-12, flux = I·Ω, symmetry; 87 tests |
| M4 Threads | `ThreadedExecutor` (atomic row counter, no locks), benchmark + chart | bit-identical for 1–64 threads; 9.9× on 24 threads |
| M5 Interop | `lumacore.dll` (C API, 13 exports), C# .NET Framework 4.8 wrapper with SafeHandle, console app | 102 C++ + 14 C# tests; C++ = C# exactly |
| M6 Polish | one-command build-and-verify script, architecture and interop docs, README | all 13 steps from a clean checkout in about 40 s |
| M7a Web | engine compiled to WebAssembly (Emscripten), interactive 3D front end (three.js/WebGL) | WebAssembly = native exactly; headless-browser screenshot |

## Problems found and how they were solved

These came from checking results against expectations, not from the compiler.

1. **Two failing geometry tests were wrong tests, not wrong code (M2).**
   A ray-triangle test expected a hit, but working it out by hand gave barycentric coordinates
   u + v = 1.087 > 1, so the point is outside and "no hit" was correct. The test became a
   must-miss case plus a correctly aimed must-hit case. A sphere test's minimum-hit threshold was a
   guess; it was replaced by rays aimed near each sphere and an independent closest-approach check.

2. **Grid points inside furniture made the statistics meaningless (M3).**
   The 0.75 m work plane cuts through a 2 m cabinet; those points reported 0 lx and E_min = 0.
   Points strictly inside a solid obstacle are now marked "not measured" (NaN) and excluded from
   E_min, E_avg and U0. Points on a surface, such as a desk top, still count.

3. **E_min was still 0 lx, and that was physically correct (M3).**
   A 5 cm gap between the cabinet and the walls receives no direct light from any luminaire
   (checked by projecting the rays). Working-area calculations exclude a 0.5 m border zone along
   the walls (EN 12464-1), so the CLI now does that by default (`--border`).

4. **C++ and C# grids differed in the 10th digit (M5).**
   The cause was the C++ CSV writer printing 10 significant digits, not the engine. With 17 digits
   (`max_digits10`), every double round-trips through text, and all 5858 values are exactly equal.
   The comparison tool was then tightened from "close enough" to "exactly equal".

5. **Speedup flattens after 12 threads; the cause was measured, not guessed (M4).**
   Repeating the benchmark on a 4× larger grid gave the same curve, which rules out thread
   start-up overhead. The remaining causes are hardware: 12 physical cores with SMT, and a lower
   all-core clock. The clock part is stated as likely, not proven.

6. **Smaller issues caught before committing.**
   A `const` executor object without a user-provided constructor, missing standard headers, and a
   headless browser serving a cached `app.js` were each caught and fixed before they reached a commit.

## Verification at the end

| Check | Result |
|---|---|
| C++ tests (GoogleTest) | 102 passed, Debug and Release |
| C# tests (xUnit, x64, .NET Framework 4.8) | 14 passed |
| Compiler warnings | 0 with MSVC `/W4 /WX`, 0 with Clang `-Wall -Wextra -Wpedantic -Wshadow -Werror` |
| Closed-form accuracy | max relative error 5.8 × 10⁻¹⁶ |
| Threaded vs sequential | bit-identical, 1–64 threads |
| C++ vs C# vs WebAssembly | 5858 of 5858 values exactly equal |
| Speedup (Ryzen 9 7900, 12C/24T) | 8.5× on 12 threads, 9.9× on 24 |
| DLL exports | exactly the 13 `lc_*` functions (`dumpbin /exports`) |

## Tools and versions

MSVC 19.44 (VS 2022 Build Tools 17.14), CMake 4.4, GoogleTest 1.15.2, .NET SDK 8.0.427 targeting
.NET Framework 4.8, xUnit 2.9, Emscripten 7.0.0, three.js 0.160, Python 3.12 with numpy and
matplotlib.

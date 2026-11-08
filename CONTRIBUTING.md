# Contributing to LumaCore
Project conventions, build commands and rules for changes. LumaCore is an educational
direct-illumination engine: point lights, 3D room geometry, ray-cast shadows, illuminance (lux) on a
measurement grid. C++17 core → C ABI DLL → C# (.NET Framework 4.8) via P/Invoke, and WebAssembly.
Full plan, risks and acceptance criteria: docs/plan.md.

## Build
- Everything, verified: `powershell -ExecutionPolicy Bypass -File scripts\build_all.ps1 [-Clean] [-Bench] [-Web]`
  (`-Web` needs the Emscripten SDK in %USERPROFILE%\emsdk; builds web/lumacore.js and checks it equals native)
  (run this before declaring any change done).
- Toolchain: VS 2022 Build Tools (MSVC x64), CMake ≥ 3.24, .NET SDK 8 (targets net48), Python 3.12.
- `cmake --preset msvc` · `cmake --build --preset msvc-release` · `ctest --preset msvc-release`
- `build/bin/Release/lumacore_geometry_demo.exe results/geometry_demo.bmp` (visual check of geometry)
- `build/bin/Release/lumacore_cli.exe office` → results/office.csv → `python tools/heatmap.py results/office.csv`
- `build/bin/Release/lumacore_bench.exe` → results/bench.csv → `python tools/plot_bench.py results/bench.csv`
  (Release only; fails if any threaded result is not bit-identical to sequential)
- `dotnet build managed/LumaCore.sln -c Release` (expects native DLL in build/bin/Release; build native first)
- `dotnet test managed/LumaCore.sln -c Release --no-build`
- `python tools/compare_grids.py results/office.csv results/office_csharp.csv` (C++ vs C# must be exactly equal)
- `python tools/heatmap.py results/grid.csv` · `python tools/plot_bench.py results/bench.csv`

## Architecture (dependencies point one way only)
math → geometry → lighting/scene → simulation → parallel → capi (lumacore.dll) → managed (C#)
- native/include/lumacore: public C++ headers; native/src: implementation (static lib lumacore_core)
- native/capi: the only ABI boundary (lumacore_c.h); managed/: C# interop + console; tools/: Python
- Executors (Sequential, Threaded, [Cuda]) implement IExecutor and share the same per-point kernel.

## Physics
E = I·cosθ / r² [lx], I in cd, r in m. Isotropic point sources, direct light only, binary hard
shadows, linear superposition, cosθ ≤ 0 → 0. Uniformity U0 = E_min / E_avg. See docs/physics.md.
Grid points strictly inside a solid occluder are NaN ("not measured") and excluded from stats;
the room is convex and its walls are not occluders.
Do not change the model without updating docs/physics.md and the analytic tests.

## C++ conventions
- C++17, MSVC /W4 /permissive- /WX /utf-8 on project targets. clang-format enforced.
- `double` for all physics on CPU. SI units; suffix names with units where ambiguous (`intensity_cd`, `height_m`).
- RAII everywhere; no owning raw pointers; `std::unique_ptr` for ownership, references for non-owning access.
- Const-correct; value types are small structs; hot-path functions `noexcept`, no allocation inside loops.
- Errors: invalid input → exceptions inside C++ (validated at construction); `std::optional` for "no hit".
- Naming: `snake_case` functions/variables, `PascalCase` types, `lc` namespace, files `snake_case.hpp/.cpp`.
- No global mutable state. Scene is immutable while a simulation runs.

## Threading rules
- Workers read shared state via `const&` only and write disjoint output indices. No locks in the hot path.
- Threaded output must be bit-identical to sequential (tested). Never change per-point summation order.

## Native API rules (native/capi)
- `extern "C"`, `__cdecl`, POD/blittable types only, opaque handles with create/destroy pairs.
- Never let a C++ exception or STL type cross the boundary: every export catches all → LcStatus.
- No `bool` in the ABI (use int32). Caller allocates output buffers; thread-local last-error message.
- Bump LC_API_VERSION on any ABI change; C# checks it at load.
- C# mirrors lumacore_c.h exactly in NativeMethods.cs; x64 only; handles wrapped in SafeHandle.

## Testing rules
- Every math/geometry/lighting function gets unit tests, including degenerate and edge cases.
- Physics validated against analytic solutions (I/h², I·h/(h²+d²)^{3/2}, superposition).
- New code is not done until tests pass and the build is warning-free. Never assert on timings.

## Milestones
M1 skeleton+toolchain · M2 math/geometry · M3 lighting+sequential sim · M4 threading
· M5 C API + C# · M6 bench+viz+docs · M7 (optional) CUDA/OpenGL.
Current milestone is tracked at the top of README.md. Work one milestone at a time.
Done beyond M6: WebAssembly + 3D web front end, EULUMDAT photometry, BVH, furniture and model import.
Remaining options: CUDA (float, CUDA ≥ 12.8 for sm_120), IES files, inter-reflections.

## Geometry rules
- Occlusion results must not depend on the BVH: any change to `bvh.cpp` keeps `Bvh.*` and
  `SceneAcceleration.*` tests green (bit-identical to brute force).
- Call `Scene::build_acceleration()` after adding geometry; geometry changes discard the tree.
- Web furniture: nothing hollow may straddle the work plane (see the header of `web/furniture.js`).

## Out of scope (unless explicitly requested)
Inter-reflections/radiosity, GUI frameworks, services,
extra dependencies beyond GoogleTest (and GLM only for comparison).

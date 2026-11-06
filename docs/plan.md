# LumaCore — Planning Document (Milestone 0)

## Context
Portfolio project: a small, correct, well-tested 3D direct-illumination engine in C++17, exposed
through a stable C ABI DLL and consumed from C# via P/Invoke. Goal: demonstrate modern C++,
computational geometry, numerics validated against analytic solutions, multithreading with measured
speedup, native/managed interop, and engineering discipline. This is the original plan, written
before any code; the [development log](development-log.md) records how it was carried out.

## 1. Starting point and prerequisites
- Greenfield project: new git repository.
- Required toolchain:

| Item | Purpose |
|---|---|
| VS 2022 Build Tools, "Desktop development with C++" | MSVC x64 compiler, Windows SDK |
| CMake ≥ 3.24 | native build (presets) |
| .NET SDK 8 | builds the `net48` projects (SDK-style csproj, reference assemblies via NuGet, no VS IDE needed) |
| .NET Framework 4.8 runtime | ships with Windows 10/11 |
| Python 3 + numpy + matplotlib | plots |
| Optional: CUDA ≥ 12.8 | GPU backend (M7); required for recent (Blackwell, sm_120) GPUs |

Install commands (elevated):
```
winget install Microsoft.VisualStudio.2022.BuildTools --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
winget install Kitware.CMake
winget install Microsoft.DotNet.SDK.8
```
Verify: `cmake --version`, `dotnet --list-sdks`, and configure a hello-world with the `Visual Studio 17 2022` generator.

## 2. Technical risks & mitigations
| Risk | Mitigation |
|---|---|
| Toolchain not installed (blocking) | Do it first; M1 acceptance includes a working build |
| Bitness mismatch → `BadImageFormatException` | DLL x64 only; C# `PlatformTarget=x64`, no AnyCPU/Prefer32Bit |
| Exceptions/STL across ABI | Every export is `noexcept` with catch-all → `LcStatus`; only POD structs, raw buffers, opaque handles |
| Ownership confusion across ABI | Opaque handle create/destroy pairs; caller-allocated output buffers; C# `SafeHandle` |
| DLL not found at runtime | CMake writes to `build/bin/<Config>`; csproj copies `lumacore.dll` to output; clear error message |
| Shadow acne / self-occlusion | Offset shadow-ray origin by `eps * n`; tMax = `dist_to_light - eps`; eps documented (1e-6 m relative) |
| Data races | Scene is immutable during simulation (`const&`); each worker writes disjoint output indices; no locks in hot path |
| No ThreadSanitizer on MSVC | Race-free by design + test that threaded output is **bit-identical** to sequential for many thread counts |
| Floating-point nondeterminism | Per-point computation identical in all backends (same loop order over lights) → exact equality achievable on CPU |
| CUDA on consumer GPU: FP64 is ~1/64 rate | GPU backend in `float`; report max relative error vs CPU double reference |
| CUDA 12.8+ / MSVC version coupling | M7 only, isolated behind `LUMACORE_ENABLE_CUDA=OFF` by default |
| Scope creep | Brute-force intersection (no BVH), direct light only, CSV output; stretch items explicitly listed |

## 3. Physics model (to be documented in `docs/physics.md`)
**E = I · cos θ / r²**
- E: illuminance at the measurement point [lx = lm/m²]; I: luminous intensity of the source toward the point [cd];
  r: source–point distance [m]; θ: angle between surface normal at the point and direction to the source.
- Assumptions: isotropic point source (I constant over directions); direct light only (no inter-reflections,
  no indirect component); opaque, binary occluders (hard shadows); no atmospheric attenuation; cos θ ≤ 0 → 0 contribution;
  multiple sources superpose linearly.
- Limitations: inverse-square law valid only at distances ≳ 5× luminaire dimension (photometric-distance rule);
  real luminaires have direction-dependent I(C, γ) (EULUMDAT/IES) — stretch goal; no maintenance factor;
  r → 0 is singular (reject points within `r_min`).
- Derived metrics: E_min, E_max, E_avg, uniformity **U0 = E_min / E_avg** (the metric used in EN 12464-1).

**Analytic validation cases (unit tests):**
1. Source at height h directly above the point: E = I / h².
2. Horizontal offset d: E = I · h / (h² + d²)^{3/2}.
3. Superposition: E(A+B) = E(A) + E(B).
4. Back-facing (cos θ < 0) → 0; fully occluded → 0.
5. (Optional) Fine grid over a large plane: ∑E·ΔA → 2πI (half of the source's total flux 4πI) — validates grid + quadrature.

## 4. Essential algorithms
- Vec3: +, −, scalar ·, dot, cross, length, normalize (zero-length → error/optional), approx-equal.
- Ray-plane: t = (d − n·o)/(n·dir); |n·dir| < ε → parallel/no hit.
- Ray-sphere: solve the quadratic with the numerically stable form (q = −½(b + sign(b)√Δ)), pick smallest t in (tMin, tMax).
- Ray-triangle: Möller–Trumbore with determinant ε test; barycentric bounds.
- Axis-aligned box → 12 triangles (helper); room = 6 inward-facing quads (walls are reflectors in future, not occluders of the interior grid).
- Measurement grid: origin, two orthonormal in-plane axes, (nx, ny) cells, cell-centered points, normal = u × v.
- Occlusion: `any_hit(ray, tMax)` with early exit, brute force over primitives.
- Parallel scheduling: worker pool over rows with an `std::atomic<size_t>` row counter (dynamic scheduling, handles occluder load imbalance).

## 5. Folder structure
```
LumaCore/
├─ CONTRIBUTING.md  README.md  CMakeLists.txt  CMakePresets.json  .gitignore  .clang-format
├─ cmake/                      # warnings, options (LUMACORE_ENABLE_CUDA, BUILD_TESTS)
├─ native/
│  ├─ include/lumacore/        # public C++ headers (engine API)
│  │  ├─ math/        vec3.hpp, ray.hpp, constants.hpp
│  │  ├─ geometry/    plane.hpp, sphere.hpp, triangle.hpp, intersection.hpp, primitives.hpp (box→tris)
│  │  ├─ lighting/    point_light.hpp, illuminance.hpp
│  │  ├─ scene/       scene.hpp, measurement_grid.hpp
│  │  ├─ simulation/  simulation.hpp (job, result, stats), executor.hpp
│  │  └─ parallel/    sequential_executor.hpp, threaded_executor.hpp
│  ├─ src/                     # matching .cpp files  → static lib `lumacore_core`
│  ├─ capi/
│  │  ├─ include/lumacore_c.h  # the ONLY header C# mirrors; pure C
│  │  └─ src/lumacore_c.cpp    # → shared lib `lumacore.dll`
│  ├─ tests/                   # GoogleTest (FetchContent, pinned tag) → `lumacore_tests`
│  ├─ bench/                   # `lumacore_bench` (std::chrono, CSV out)
│  ├─ apps/cli/                # `lumacore_cli`: run scenario → CSV (handy for Python w/o .NET)
│  └─ cuda/                    # M7, optional
├─ managed/
│  ├─ LumaCore.sln
│  ├─ LumaCore.Interop/        # net48 class lib: NativeMethods, SafeHandles, Scene, exceptions
│  ├─ LumaCore.Console/        # net48 console app (x64)
│  └─ LumaCore.Interop.Tests/  # xUnit, net48 x64
├─ tools/                      # heatmap.py, plot_bench.py, requirements.txt
├─ scenes/                     # example scenario definitions (simple key=value or JSON-free text)
├─ docs/                       # architecture.md, physics.md, benchmarks.md (+ images)
└─ results/                    # generated CSV/PNG (gitignored except curated samples)
```

## 6. Module responsibilities & dependency rules
`math` ← `geometry` ← `lighting` / `scene` ← `simulation` ← `parallel` ← `capi` ← C#.
Dependencies point only leftward; `math`/`geometry` have zero I/O and no threading.
- **math**: value types, pure `constexpr`/`noexcept` functions.
- **geometry**: primitives + intersection queries returning `std::optional<Hit>`.
- **lighting**: `PointLight{position, intensity_cd}`; `direct_illuminance(point, normal, light, occlusion)`.
- **scene**: owns geometry and lights (value semantics, `std::vector`); validates inputs; `occluded()` query; immutable during a run.
- **simulation**: `SimulationJob` (const Scene&, grid, options), `SimulationResult` (lux vector, nx, ny, stats, timing); computes stats.
- **parallel**: `IExecutor` strategy — `SequentialExecutor`, `ThreadedExecutor(n)`, later `CudaExecutor`.
- **capi**: translation layer only — validates args, converts C structs, catches all exceptions, thread-local last-error string.
- **C# Interop**: P/Invoke declarations, `SafeHandle`, idiomatic `IDisposable` wrappers, status → `LumaCoreException`.

## 7. Interfaces
**C++ core (sketch):**
```cpp
namespace lc {
struct Vec3 { double x{}, y{}, z{}; };                      // + free constexpr ops, dot, cross, length
std::optional<Vec3> normalized(const Vec3&) noexcept;
class Ray { public: static std::optional<Ray> make(Vec3 o, Vec3 d); /* dir unit-length invariant */ };
struct Hit { double t; Vec3 normal; };
std::optional<Hit> intersect(const Ray&, const Plane&,    double tMin, double tMax) noexcept;
std::optional<Hit> intersect(const Ray&, const Sphere&,   double tMin, double tMax) noexcept;
std::optional<Hit> intersect(const Ray&, const Triangle&, double tMin, double tMax) noexcept;
class Scene { public: void add_light(PointLight); void add_box(Vec3 min, Vec3 max); void add_sphere(Sphere);
              bool occluded(const Ray&, double maxDist) const noexcept; /* ... */ };
class MeasurementGrid { public: MeasurementGrid(Vec3 origin, Vec3 u, Vec3 v, size_t nx, size_t ny);
              Vec3 point(size_t i, size_t j) const noexcept; Vec3 normal() const noexcept; };
double illuminance_at(const Scene&, const Vec3& p, const Vec3& n, bool shadows) noexcept; // shared kernel
class IExecutor { public: virtual ~IExecutor() = default;
              virtual void run(const SimulationJob&, double* out, size_t count) const = 0; };
SimulationResult simulate(const SimulationJob&, const IExecutor&);  // validates, times, computes stats
}
```
Invalid construction input → exception inside C++ (`std::invalid_argument`); hot path is `noexcept`.

**C ABI (`lumacore_c.h`):** `extern "C"`, `__cdecl`, `LC_API` = dllexport/dllimport macro.
```c
typedef struct LcScene LcScene;                                   /* opaque */
typedef enum { LC_OK = 0, LC_ERR_NULL_ARG, LC_ERR_INVALID_ARG, LC_ERR_BUFFER_TOO_SMALL,
               LC_ERR_OUT_OF_MEMORY, LC_ERR_INTERNAL } LcStatus;  /* int32 */
typedef struct { double x, y, z; } LcVec3;
typedef struct { LcVec3 origin, u_axis, v_axis; uint32_t nx, ny; } LcGridDesc;
typedef struct { int32_t backend; /*0 seq,1 threads,2 cuda*/ uint32_t threads; int32_t shadows; } LcSimOptions;
typedef struct { double min_lux, max_lux, avg_lux, uniformity_u0, elapsed_ms; } LcSimStats;

uint32_t lc_api_version(void);
LcStatus lc_scene_create(LcScene** out);
void     lc_scene_destroy(LcScene* s);                  /* null-safe */
LcStatus lc_scene_set_room(LcScene*, double width, double depth, double height);
LcStatus lc_scene_add_point_light(LcScene*, LcVec3 pos, double intensity_cd);
LcStatus lc_scene_add_box(LcScene*, LcVec3 min, LcVec3 max);
LcStatus lc_scene_add_sphere(LcScene*, LcVec3 center, double radius);
LcStatus lc_simulate(const LcScene*, const LcGridDesc*, const LcSimOptions*,
                     double* out_lux, size_t out_count, LcSimStats* out_stats);  /* caller-allocated, nx*ny */
LcStatus lc_last_error_message(char* buf, size_t buf_size, size_t* required);    /* thread-local, UTF-8 */
```
Rules: blittable types only, no `bool` (use int32), no C++ types, ABI version checked by C# at load.

**C#:** `NativeMethods` (`[DllImport("lumacore", CallingConvention = Cdecl)]`), `SceneHandle : SafeHandleZeroOrMinusOneIsInvalid`,
public `Scene : IDisposable` (`AddLight`, `AddBox`, `Simulate(GridDesc, SimOptions) → SimulationResult` with `double[]`),
`LumaCoreException(LcStatus, message)`.

## 8. Milestones, acceptance criteria, 3-day schedule
| # | Milestone | Acceptance criteria | Day |
|---|---|---|---|
| M1 | Repo, toolchain, CMake skeleton, CONTRIBUTING.md, GTest wired | `cmake --preset msvc-release && cmake --build` succeeds; one trivial test passes; /W4 /permissive- /WX clean | 1 |
| M2 | Math + geometry | Unit tests: vector identities (cross ⟂ operands, |normalize|=1, zero-vector rejection), plane/sphere/triangle hit/miss/tangent/parallel/behind-origin/edge cases with known t; ≥ 30 tests green | 1 |
| M3 | Lighting + scene + grid + sequential sim | Analytic cases 1–4 within 1e-9 relative; occluder box blocks expected cells; stats (min/max/avg/U0) correct on hand-computed grid; CLI writes CSV | 2 |
| M4 | Threaded executor | Output **bit-identical** to sequential for threads ∈ {1,2,3,7,24} incl. nx·ny not divisible by threads; invalid thread count handled; bench shows speedup vs threads | 2 |
| M5 | C API DLL + C# P/Invoke | Exports verified (`dumpbin /exports`); every export survives null/invalid args returning status; C# console runs a scene; xUnit test reproduces analytic E via P/Invoke; no leaks (handle disposed, destroy null-safe) | 3 |
| M6 | Benchmarks + visualization + docs | `bench` CSV (backend, threads, points, lights, prims, median ms of N runs after warm-up); `plot_bench.py` speedup/efficiency chart; `heatmap.py` lux heatmap with luminaire positions; README with results + architecture diagram | 3 |
| M7 | Optional: CUDA backend, OpenGL viewer | `CudaExecutor` behind CMake option; max rel. error vs CPU (float vs double) reported; speedup reported | post |

**Realistic in 3 days:** M1–M6 with a clean README. M7 not in scope.
**Stretch priority if time remains (ranked by relevance to lighting design):**
1. EULUMDAT (.ldt) photometric intensity distribution I(C, γ) replacing isotropic I: the standard input of real lighting design.
2. Simple BVH/AABB culling with before/after benchmark.
3. CUDA backend (RTX 5060 Ti, float, CUDA ≥ 12.8).
4. OpenGL viewer.

**Main engineering points to demonstrate:** analytic-validated numerics, the clean C ABI + SafeHandle interop, measured parallel speedup with determinism guarantee, U0/EN 12464-1 vocabulary, and a readable architecture doc.

## 9. Testing strategy
- GoogleTest for C++ (pinned via FetchContent); fixtures for scenes; tolerance helpers (`EXPECT_NEAR` with relative tol).
- Property-style loops over randomized (seeded) inputs for vector identities and ray–triangle consistency.
- C API tests in C++ (call the exported functions directly, check error paths) + C# xUnit for marshalling.
- Optional: MSVC `/fsanitize=address` Debug preset.
- Benchmarks are not tests; never assert on timing.

## 10. CONTRIBUTING.md (to be written verbatim at repo root in M1)
```markdown
# LumaCore — C++ Computational Lighting Engine
Educational direct-illumination engine: point lights, 3D room geometry, ray-cast shadows,
illuminance (lux) on a measurement grid. C++17 core → C ABI DLL → C# (.NET Framework 4.8) via P/Invoke.

## Build
- Toolchain: VS 2022 Build Tools (MSVC x64), CMake ≥ 3.24, .NET SDK 8 (targets net48), Python 3.12.
- `cmake --preset msvc` · `cmake --build --preset msvc-release` · `ctest --preset msvc-release`
- `dotnet build managed/LumaCore.sln -c Release` (expects native DLL in build/bin/Release)
- `python tools/heatmap.py results/grid.csv` · `python tools/plot_bench.py results/bench.csv`

## Architecture (dependencies point one way only)
math → geometry → lighting/scene → simulation → parallel → capi (lumacore.dll) → managed (C#)
- native/include/lumacore: public C++ headers; native/src: implementation (static lib lumacore_core)
- native/capi: the only ABI boundary (lumacore_c.h); managed/: C# interop + console; tools/: Python
- Executors (Sequential, Threaded, [Cuda]) implement IExecutor and share the same per-point kernel.

## Physics
E = I·cosθ / r² [lx], I in cd, r in m. Isotropic point sources, direct light only, binary hard
shadows, linear superposition, cosθ ≤ 0 → 0. See docs/physics.md. Do not change the model
without updating docs/physics.md and the analytic tests.

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
- Caller allocates output buffers; thread-local last-error message; bump LC_API_VERSION on any ABI change.
- C# mirrors lumacore_c.h exactly in NativeMethods.cs; x64 only; handles wrapped in SafeHandle.

## Testing rules
- Every math/geometry/lighting function gets unit tests, including degenerate and edge cases.
- Physics validated against analytic solutions (I/h², I·h/(h²+d²)^{3/2}, superposition).
- New code is not done until tests pass and the build is warning-free. Never assert on timings.

## Milestones
M1 skeleton+toolchain · M2 math/geometry · M3 lighting+sequential sim · M4 threading
· M5 C API + C# · M6 bench+viz+docs · M7 (optional) CUDA/OpenGL.
Current milestone is tracked at the top of README.md. Work one milestone at a time.

## Out of scope (unless explicitly requested)
Inter-reflections/radiosity, BVH, GUI frameworks, services, extra dependencies beyond GTest (and GLM only for comparison).
```

## 11. Verification (for the implementation phases)
- After M1: configure/build/ctest from a clean `build/` dir.
- Each milestone: `ctest` all green, zero warnings; M4 determinism test; M5 `dumpbin /exports lumacore.dll`
  + `dotnet test` + run `LumaCore.Console`; M6 regenerate PNGs from CSV and inspect heatmap symmetry
  (a centered light in a symmetric room must yield a symmetric heatmap).

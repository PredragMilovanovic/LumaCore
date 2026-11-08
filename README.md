# LumaCore

**[▶ Live 3D demo](https://predragmilovanovic.github.io/LumaCore/)**: a furnished open-plan office
in your browser, lit by calculation: the C++ engine (compiled to WebAssembly) computes the light on
the floor, the walls and every piece of furniture. Drag luminaires, desks, chairs or shelves and the
light is recalculated, including the shadows of every monitor, chair and plant. Import
your own 3D model (`.glb`/`.obj`) and it casts real shadows too. Also try the original office with
[linear office luminaires](https://predragmilovanovic.github.io/LumaCore/?luminaire=linear_batwing),
[spotlights](https://predragmilovanovic.github.io/LumaCore/?luminaire=spot_narrow), or load your
own manufacturer `.ldt` file. Move and resize the furniture, change the room, export the grid as
CSV, and share the whole design as a link.

A C++17 engine that computes **direct illuminance (lux)** from point light sources on a
measurement grid inside a 3D room, with ray-cast shadows. It runs sequentially or on all CPU
cores, and it is exposed as a native Windows DLL with a C API that a **C# (.NET Framework 4.8)**
application calls through P/Invoke.

It is an educational engine, not a commercial lighting simulator: direct light only, point
sources (isotropic or with measured EULUMDAT distributions), hard shadows.
[docs/physics.md](docs/physics.md) states the model, its assumptions and its limits.

![Office illuminance heatmap with isolux lines, luminaires and obstacle shadows](docs/images/office_heatmap.png)

*A 6 × 4 m office with six 800 cd luminaires, a cabinet, a partition and a pendant globe. Values
are on the 0.75 m work plane, with the 0.5 m border zone excluded. E_avg 414 lx, U0 = 0.25.*

### Interactive 3D in the browser

The same C++ engine, compiled to WebAssembly, drives a three.js (WebGL) view of the room. The
furniture is about 5,000 shadow-casting triangles; with a bounding volume hierarchy, the whole
office (14 luminaires, 14,000 points) is recalculated in about 50 ms on one browser thread while you
drag. The panel evaluates the desk working zones against the usual office target (500 lx, U0 ≥ 0.60).
**[Open the live demo](https://predragmilovanovic.github.io/LumaCore/)** · details: [docs/web.md](docs/web.md).

![LumaCore Web: the whole room lit by the computed light](docs/images/web_room_appearance.png)

*Whole-room view: the brightness of every floor, wall and furniture point is the C++ engine's
direct illuminance at that point (E = I cos θ / r² with shadow rays), times the surface colour.
Shadows under the desks, chairs and plants are calculated, not drawn by the graphics library.*

![LumaCore Web: work plane, light appearance view](docs/images/web_furnished_appearance.png)

![LumaCore Web: the same office with the lux map](docs/images/web_furnished_lux.png)

## Highlights

| | |
|---|---|
| **Verified numerics** | Illuminance matches the closed form I·h/(h²+d²)^{3/2} to a relative 6×10⁻¹⁶. Flux through a square matches I·Ω (exact solid angle) to within 10⁻⁴. Every ray-primitive intersection is tested at its edge cases and with seeded randomized property tests. |
| **Parallel and deterministic** | `ThreadedExecutor` reaches **8.5× on 12 cores and 9.9× on 24 threads** (Ryzen 9 7900), and its output is **bit-identical** to the sequential run for any thread count. [Benchmarks](docs/benchmarks.md) |
| **Clean native/managed boundary** | A plain C API (13 functions, fixed-width types, opaque handle, caller-owned buffers, no exceptions across the boundary). The C# side uses a `SafeHandle` and turns native errors into exceptions. C++ and C# produce **exactly equal** grids. [Interop](docs/interop.md) |
| **Real luminaire data** | Reads **EULUMDAT (.ldt)** photometry: all five symmetry types, bilinear interpolation in C and γ, rotation of the luminaire. Verified by integrating the distribution: luminaire flux = lamp flux × LOR within 0.1 %. |
| **One engine, three platforms** | The unchanged C++ sources also compile to **WebAssembly** (Clang, `-Wall -Wextra -Werror`). Native C++, C# through the DLL, and the browser produce **bit-identical** results for isotropic sources, and agree to the last bit's rounding (4 × 10⁻¹⁶) with photometric luminaires, where MSVC's and Emscripten's `atan2`/`acos` may round differently. |
| **Fast shadows (BVH)** | A bounding volume hierarchy over the occluder triangles makes shadow tests logarithmic: **159× faster at 12,000 triangles and 274× at 48,000**, with results bit-identical to testing every triangle (tested with 20,000 random rays plus edge, vertex and axis-parallel cases). [Benchmarks](docs/benchmarks.md) |
| **Tested and reproducible** | 121 C++ tests (GoogleTest) and 14 C# tests (xUnit). One script builds and verifies everything from a clean checkout in about 40 s. No warnings at `/W4 /WX`. |

## Quick start

Requirements (Windows 10/11, x64):

- Visual Studio 2022 Build Tools with **Desktop development with C++** (MSVC, Windows SDK)
- CMake 3.24 or later
- .NET SDK 8 or later (it builds the .NET Framework 4.8 projects; no Visual Studio IDE needed)
- Python 3 with `numpy` and `matplotlib` (`pip install -r tools/requirements.txt`)

Build, test and verify everything, then open the plots:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_all.ps1 -Clean -Bench
start results\office.png
```

The script stops at the first failure. It runs the native build and tests, the managed build and
tests, the C++ CLI and the C# console, checks that their results are identical, and draws the
heatmaps and the benchmark chart.

## Architecture

```
math → geometry → lighting / scene → simulation → parallel → lumacore.dll (C API) → C# (P/Invoke)
```

- `native/` is a static C++ library (`lumacore_core`), the C API DLL (`lumacore.dll`), the tests,
  the CLI, the benchmark and a geometry demo.
- `managed/` is `LumaCore.Interop` (P/Invoke and an idiomatic C# API), `LumaCore.Console`, and the
  xUnit tests.
- Every executor calls one shared per-row kernel, so the sequential executor is the reference for
  all the others.

Diagrams, design decisions and their alternatives: [docs/architecture.md](docs/architecture.md).

Using it from C#:

```csharp
using (var scene = new LightingScene())
{
    scene.SetRoom(6, 4, 3);
    scene.AddPointLight(new Vec3(3, 2, 2.8), 800.0);       // position [m], intensity [cd]
    scene.AddBox(new Vec3(2.9, 0.2, 0), new Vec3(3.1, 1.6, 1.6));
    var grid = MeasurementGrid.Horizontal(0.5, 0.5, 5.5, 3.5, 0.75, 100, 60);
    SimulationResult r = scene.Simulate(grid);              // all cores, shadows on
    Console.WriteLine($"E_avg {r.AvgLux:F0} lx, U0 {r.UniformityU0:F2}");
}
```

## Running the pieces

**Lux heatmap of a room (C++):**

```powershell
build\bin\Release\lumacore_cli.exe office              # or: single
python tools\heatmap.py results\office.csv
```

Options: `--cell <m>` (grid spacing, default 0.05), `--border <m>` (excluded band along the walls,
default 0.5), `--no-shadows`, `--threads <n>` or `--sequential`, `--ldt <file>` with optional
`--rotation <deg>` (give every lamp a measured light distribution), `--out <file.csv>`. The `single`
scenario also prints the error against the closed-form solution.

```powershell
build\bin\Release\lumacore_cli.exe office --ldt samples\ldt\linear_batwing.ldt --out results\office_linear.csv
python tools\heatmap.py results\office_linear.csv
```

**The same calculation from C# through the DLL:**

```powershell
managed\LumaCore.Console\bin\Release\net48\LumaCore.Console.exe --csv results\office_csharp.csv
python tools\compare_grids.py results\office.csv results\office_csharp.csv
```

**Multithreading benchmark:**

```powershell
build\bin\Release\lumacore_bench.exe                     # fails if any result is not bit-identical
python tools\plot_bench.py results\bench.csv
```

![Speedup and efficiency vs thread count](docs/images/bench_speedup.png)

**3D web version** (after `scripts\build_wasm.ps1`, see [docs/web.md](docs/web.md)):

```powershell
python -m http.server 8000 --directory web
start http://localhost:8000
```

**Geometry demo.** A ray-cast image built only from the intersection code:

```powershell
build\bin\Release\lumacore_geometry_demo.exe results\geometry_demo.bmp
```

**Tests only:**

```powershell
cmake --preset msvc
cmake --build --preset msvc-release
ctest --preset msvc-release
dotnet build managed\LumaCore.sln -c Release
dotnet test managed\LumaCore.sln -c Release --no-build
```

## Repository layout

| Path | Contents |
|---|---|
| `native/include/lumacore`, `native/src` | C++ engine: math, geometry, lighting, scene, simulation, parallel |
| `native/capi` | C API, the only binary boundary (`lumacore.dll`) |
| `native/tests` | GoogleTest unit, property, concurrency and C API tests |
| `native/apps`, `native/bench` | `lumacore_cli`, `lumacore_geometry_demo`, `lumacore_bench`, shared scenarios |
| `managed/` | `LumaCore.Interop`, `LumaCore.Console`, `LumaCore.Interop.Tests` |
| `native/wasm`, `web/` | WebAssembly bindings and the 3D browser front end (`index.html`, `app.js`, `furniture.js`, generated `lumacore.js`) |
| `samples/ldt` | synthetic EULUMDAT luminaires (downlight, spot, linear), made by `tools/make_sample_ldt.py` |
| `tools/` | `heatmap.py`, `plot_bench.py`, `compare_grids.py`, `wasm_check.mjs`, `make_sample_ldt.py` |
| `scripts/build_all.ps1` | build and verify everything |
| `docs/` | [architecture](docs/architecture.md), [physics](docs/physics.md), [interop](docs/interop.md), [benchmarks](docs/benchmarks.md), [web](docs/web.md), [development log](docs/development-log.md), [plan](docs/plan.md) |

## Limitations and next steps

- **Point-source approximation.** Luminaires use their measured distribution but are treated as
  points, which holds beyond about 5× the luminaire size. IES files are not read yet (EULUMDAT is).
- **Direct light only.** No inter-reflections, so values are below those in a real room with
  light-coloured surfaces.
- **Hard shadows.** Long luminaires are single points, so monitor shadows have sharp edges.
  Splitting a linear luminaire into 9 segments was tested: task-area U0 rises from 0.53 to 0.58.
- **No GPU backend yet.** A CUDA executor would plug into the same `IExecutor` interface, with
  results compared against the CPU reference.
- **Deployment.** `lumacore.dll` needs the Visual C++ 2015–2022 runtime (x64) on machines without
  Visual Studio.

## Development

Built milestone by milestone (plan and acceptance criteria in [docs/plan.md](docs/plan.md)):

| # | Scope | Status |
|---|---|---|
| M1 | Repository, CMake, GoogleTest, conventions | done |
| M2 | Vector maths and ray intersections | done |
| M3 | Illuminance model, scene, sequential simulation | done |
| M4 | Multithreaded execution and benchmark | done |
| M5 | C API DLL and C# P/Invoke | done |
| M6 | Build-and-verify script, architecture and documentation | done |
| M7a | WebAssembly build and interactive 3D web front end (WebGL) | done |
| M7b | EULUMDAT photometry: reader, rotation, CLI, web (luminaire choice, `.ldt` upload, polar diagram) | done |
| M7c | Editable room in the web demo (move/resize/add obstacles, room size), CSV/PNG export, share links | done |
| M7d | BVH acceleration; furnished office with procedural furniture; 3D model import; task-area evaluation; light-appearance view | done |
| M7e | Optional: CUDA, IES files, inter-reflections | planned |

Coding conventions and project rules: [CONTRIBUTING.md](CONTRIBUTING.md).

# LumaCore Web

An interactive 3D version of the office scenario that runs entirely in the browser. The
**unchanged C++ engine** is compiled to WebAssembly with Emscripten; three.js (WebGL) draws
the room. Drag a lamp, and the illuminance map is recalculated by the C++ code immediately.

```
native/src/**.cpp ──em++──► web/lumacore.js (WebAssembly, embedded) ◄── web/app.js (three.js UI)
        same sources as lumacore.dll and the CLI          Embind bindings: native/wasm/lumacore_wasm.cpp
```

## What it shows

- The room, obstacles and lamps in 3D (rotate, pan, zoom).
- The lux map on the work plane, with the same colour scale as the PNG heatmaps. Hover a point
  to read its value.
- Live statistics: E_avg, E_min, E_max, U0, number of points and calculation time.
- Controls: drag lamps, change the work-plane height and the grid spacing, switch shadows on or
  off, add and remove lamps.
- Luminaires: per lamp, choose an isotropic point source or a light distribution from an
  EULUMDAT file (three synthetic samples, or **load any manufacturer `.ldt` file** from your
  computer; it is parsed by the C++ engine in the browser). Set dimming and the rotation of the
  C0 axis; the polar diagram shows the C0–C180 and C90–C270 planes, and asymmetric luminaires
  show their C0 axis as a bar in 3D. "Apply to all lamps" copies the settings.
- Shareable links: `?luminaire=linear_batwing` (or `downlight_wide`, `spot_narrow`), optionally
  with `&rotation=90&dimming=80`, opens the room with every lamp set that way.

## Why single-threaded in the browser

Browser threads need `SharedArrayBuffer`, which is only available on pages served with
cross-origin isolation headers (COOP/COEP). Static hosts such as GitHub Pages cannot send them,
so the web build uses the `SequentialExecutor`. At the default 5 cm grid (6,000 points) one
calculation takes a few milliseconds, which is fast enough to recompute on every frame while a
lamp is dragged.

## Build

One-time setup: the Emscripten SDK, installed into `%USERPROFILE%\emsdk` (about 1 GB):

```powershell
cd $env:USERPROFILE
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
.\emsdk.bat install latest
.\emsdk.bat activate latest
```

Then, from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_wasm.ps1   # writes web/lumacore.js
```

The script activates Emscripten only for itself and compiles with `-Wall -Wextra -Wpedantic
-Wshadow -Werror`, so the engine also builds cleanly with Clang, not only MSVC.

## Verify

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_all.ps1 -Web
```

With `-Web`, the build script also builds the WebAssembly version, runs `tools/wasm_check.mjs` in
the Node.js bundled with Emscripten (closed-form 250 lx case, error reporting, office grid), and
runs `tools/compare_grids.py` against the native C++ result. Result for isotropic lamps: all 5858
measured values are **exactly equal**, so C++ native, C# through the DLL and the browser give
identical numbers. With EULUMDAT luminaires the values agree within 4 × 10⁻¹⁶ (relative): the
direction-to-angle conversion uses `atan2`/`acos`, whose last bit MSVC's and Emscripten's maths
libraries may round differently. That comparison runs with `--rel-tol 1e-12`.

## Run locally

ES modules do not load from `file://`, so serve the folder:

```powershell
python -m http.server 8000 --directory web
start http://localhost:8000
```

## Publish

`web/` is a static site: `index.html`, `app.js` and the generated `lumacore.js`. Any static host
works, for example GitHub Pages ("Deploy from a branch", folder `/web` via a workflow, or a
`gh-pages` branch with the contents of `web/`).

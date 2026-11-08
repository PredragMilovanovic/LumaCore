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
- Editable room: click an obstacle to select it, drag it to move it, resize it with sliders
  (boxes: width, depth, height; spheres: radius and height), add desks ("Add box"), remove
  anything. Change the room size; lamps stay 0.2 m below the ceiling and everything is kept
  inside the room.
- Export: **Download CSV** writes the grid in the same format as `lumacore_cli`, so
  `tools/heatmap.py` and `tools/compare_grids.py` work on it. **Download 3D image** saves the
  view as PNG.
- **Copy share link** puts the whole design (room, lamps, luminaires, dimming, rotation,
  obstacles, settings) in the URL fragment (`#s=…`, base64url JSON). Opening the link restores
  it. Every value is range-checked when a link is read, and a damaged link falls back to the
  default office with a message. Uploaded `.ldt` files are not included (too large for a URL).
- Quick links: `?luminaire=linear_batwing` (or `downlight_wide`, `spot_narrow`), optionally
  with `&rotation=90&dimming=80`, opens the office with every lamp set that way.

## Furnished office (default scene)

`web/furniture.js` builds furniture from code: workstations (desk, monitor, keyboard, office chair
on a five-star base), a meeting table with four chairs, bookshelves with books, a tall cabinet,
acoustic partitions and plants. **The triangles that are drawn are the triangles the engine uses for
shadows**, with three documented exceptions that follow one physics rule: *nothing hollow may
straddle the work plane* (a measured point inside a closed shape can never see a lamp):

- Objects lying **on** the work plane (keyboard, mouse, monitor foot) are drawn but cast no shadow:
  the plane is measured at their bottom face.
- A closed part that **crosses** the plane (the chair backrest) casts its shadow through its
  mid-plane, a zero-thickness panel, so no point can be trapped underneath or inside it.
- Closed furniture (bookshelf, cabinet, partition) is a **solid block** for the engine, so points
  inside it are "not measured", exactly like the boxes of the reference office. These items rotate in
  90° steps because engine boxes are axis-aligned.

Both rules came from diagnostics: the first furnished version had E_min = 0 lx from points trapped
under a keyboard and between a chair backrest and its support bar.

**Import a 3D model** (`.glb`, embedded `.gltf` or `.obj`): it is turned upright (glTF is Y-up),
scaled to about 1.5 m if it is far too large or small, and placed on the floor. Every triangle casts
shadows (limit 300,000). Free models, for example the Khronos glTF sample models, work.

**Luminaires** are drawn at their real size from the EULUMDAT file (length, width and height:
linear luminaires as suspended boxes, round ones as discs), with suspension wires.

**Task areas.** EN 12464-1 sets office requirements on the task area. The tiles therefore show the
desk working zones (full desk width, from the front edge to the monitor) and the meeting table,
against the usual office target of 500 lx and U0 ≥ 0.60 (an indicative check). The whole work plane
is shown below them; its minimum includes the true 0 lx pockets right behind chair backs, which a
direct-light, point-source model produces.

The default luminaire layout was chosen by comparing five layouts on these values. A finding: rows
of luminaires behind the monitors give the lowest minimum, because a screen blocks the light from
behind it onto the keyboard area. The chosen layout reaches 564 lx and U0 0.53 on the task areas, so
uniformity misses the target, and the panel says so. The model is pessimistic: without inter-reflections
and with hard shadows, a real room would be more uniform.

**Views.** *Work plane* views colour only the 0.75 m calculation surface (lux scale, or dark to warm
white). *Whole room* views (the default) colour every surface by its computed light:

- The engine's `evaluatePoints()` returns the direct illuminance at any point with any surface
  direction, using the same `illuminance_at()` as the work plane (checked in `tools/wasm_check.mjs`
  against E = I cos θ / r² on a wall point, to the last digit).
- Floor and the two far walls are 12 cm grids; furniture faces are split into triangles of at most
  15 cm, so shadows across a desk top are visible. Points shared by several small triangles are
  evaluated once (225,391 → 46,446 points for the furnished office, about 0.45 s at the measured
  9.6 µs per point), in chunks of 25,000, so the page never freezes; a newer request cancels an
  older one.
- Evaluation points sit 2 mm in front of the surface, so a ray from a corner does not hit the
  neighbouring face of the same object. Points inside a solid item (the books inside a bookshelf,
  which the engine treats as one block) are moved along their normal to the item's outer shell.
- *Light appearance* shows each surface's own colour times its tone-mapped illuminance: a matte
  surface's luminance is ρ·E/π, so a red book stays red and gets darker in shadow. *Lux* shows the
  false-colour scale on every surface.
- While something is dragged, the live work-plane view is shown; the whole room is recomputed when
  it is released.
- Expected with direct light only: the top of the walls (above the luminaires) and the ceiling are
  dark, because these luminaires emit nothing upward and no light is reflected.

**Rendering on demand.** The 3D view is redrawn only when something changes (camera, objects,
results), not 60 times per second, which saves CPU and battery and lets headless tests finish.

Boxes are stored by their exact corners, not centre and size, so the default office matches the
C++ scenario bit for bit (`0.35 - 0.3` is not exactly `0.05` in floating point). A share link
with the native `single` scenario's setup reproduces its average to all printed digits.

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

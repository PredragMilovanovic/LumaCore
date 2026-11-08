// Checks of the web demo's light calculation and geometry, in Node.js (no browser):
//   physics   energy conservation in a closed room (isotropic and EULUMDAT luminaire), shadows
//             on surfaces, luminaire rotation
//   geometry  the furniture rule "nothing hollow straddles the work plane", solid boxes,
//             the surface helpers (tessellation, merged vertices, evaluation points)
//   scenes    regression values of the furnished office (task areas, whole plane)
//
// Usage (from the repository root, Node 18+):  node tools/web_check.mjs

import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const load = (file) => import(pathToFileURL(join(root, 'web', file)).href);
const { default: createLumaCore } = await load('lumacore.js');
const { CATALOG, itemGeometry, placePoints, placedSolid } = await load('furniture.js');
const { tessellate, flatNormals, evaluationPoints, uniqueVertices } = await load('surfaces.js');
const { furnishedScene, referenceScene, furnitureForEngine, footprint, taskStats } = await load('scene.js');
const lc = await createLumaCore();
const ldt = (name) => readFileSync(join(root, 'samples', 'ldt', `${name}.ldt`), 'utf8');

let failures = 0;
function check(name, condition, detail = '') {
  console.log(`${condition ? '[ok]  ' : '[FAIL]'} ${name}${detail ? `  (${detail})` : ''}`);
  if (!condition) failures += 1;
}
const rel = (a, b) => Math.abs(a - b) / Math.abs(b);

// ---- Physics: energy conservation ------------------------------------------------------
// In a closed room without obstacles, every lumen a luminaire emits lands on a room surface, so
// the sum of E x area over floor, ceiling and four walls must equal the luminaire's flux.

/// Midpoint grid on all six inner surfaces of a W x D x H room: { points, normals, area }.
function roomSurfaces(W, D, H, step) {
  const points = [], normals = [], areas = [];
  const face = (nu, nv, du, dv, origin, u, v, n) => {
    for (let a = 0; a < nu; ++a) {
      for (let b = 0; b < nv; ++b) {
        const s = (a + 0.5) * du, t = (b + 0.5) * dv;
        points.push(origin[0] + u[0] * s + v[0] * t, origin[1] + u[1] * s + v[1] * t, origin[2] + u[2] * s + v[2] * t);
        normals.push(...n);
        areas.push(du * dv);
      }
    }
  };
  const nx = Math.round(W / step), ny = Math.round(D / step), nz = Math.round(H / step);
  face(nx, ny, W / nx, D / ny, [0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 1]);    // floor
  face(nx, ny, W / nx, D / ny, [0, 0, H], [1, 0, 0], [0, 1, 0], [0, 0, -1]);   // ceiling
  face(nx, nz, W / nx, H / nz, [0, 0, 0], [1, 0, 0], [0, 0, 1], [0, 1, 0]);    // wall y = 0
  face(nx, nz, W / nx, H / nz, [0, D, 0], [1, 0, 0], [0, 0, 1], [0, -1, 0]);   // wall y = D
  face(ny, nz, D / ny, H / nz, [0, 0, 0], [0, 1, 0], [0, 0, 1], [1, 0, 0]);    // wall x = 0
  face(ny, nz, D / ny, H / nz, [W, 0, 0], [0, 1, 0], [0, 0, 1], [-1, 0, 0]);   // wall x = W
  return { points: Float64Array.from(points), normals: Float64Array.from(normals), areas };
}

function fluxOnRoom(engine, W, D, H, step) {
  const s = roomSurfaces(W, D, H, step);
  const r = engine.evaluatePoints(s.points, s.normals, true);
  if (!r.ok) throw new Error(r.error);
  let flux = 0;
  for (let k = 0; k < s.areas.length; ++k) flux += r.lux[k] * s.areas[k];
  return flux;
}

{
  const engine = new lc.Engine();
  engine.addLight(2, 1.5, 1.25, 1000);
  const flux = fluxOnRoom(engine, 4, 3, 2.5, 0.025);
  const expected = 4 * Math.PI * 1000;
  check('energy: isotropic 1000 cd, Σ E·A over the room = 4π·I', rel(flux, expected) < 2e-3,
    `${flux.toFixed(1)} lm vs ${expected.toFixed(1)} lm, ${(100 * rel(flux, expected)).toFixed(3)} %`);
  engine.delete();
}
{
  const engine = new lc.Engine();
  const id = engine.addPhotometry(ldt('linear_batwing'));
  engine.addPhotometricLight(2, 1.5, 2.2, id, 30, 1);
  const flux = fluxOnRoom(engine, 4, 3, 2.5, 0.025);
  const expected = engine.photometryInfo(id).fluxLm;
  check('energy: EULUMDAT linear luminaire, Σ E·A = luminaire flux', rel(flux, expected) < 3e-3,
    `${flux.toFixed(1)} lm vs ${expected.toFixed(1)} lm, ${(100 * rel(flux, expected)).toFixed(3)} %`);
  engine.delete();
}

// ---- Physics: shadows and rotation on surfaces -----------------------------------------
{
  const engine = new lc.Engine();
  engine.addLight(0, 0, 3, 900);
  engine.addBox(-0.5, -0.5, 1, 0.5, 0.5, 2);
  const pts = new Float64Array([0, 0, 0.002, 2, 0, 0.002]), nor = new Float64Array([0, 0, 1, 0, 0, 1]);
  // r.lux is a view into WebAssembly memory, valid only until the next engine call: copy it.
  const lit = Float64Array.from(engine.evaluatePoints(pts, nor, true).lux);
  const free = Float64Array.from(engine.evaluatePoints(pts, nor, false).lux);
  check('shadow on the floor under a box is 0 lx; without shadows I/h²',
    lit[0] === 0 && Math.abs(free[0] - 900 / (2.998 ** 2)) < 1e-9, `${lit[0]} / ${free[0].toFixed(3)}`);
  const expectedSide = 900 * 2.998 / (4 + 2.998 ** 2) ** 1.5;
  check('floor point beside the box is unshadowed (E = I h / r³)', Math.abs(lit[1] - expectedSide) < 1e-9 * expectedSide);
  engine.delete();
}
{
  // Rotating a luminaire by 90° about the vertical axis must rotate its light pattern with it.
  const engine = new lc.Engine();
  const id = engine.addPhotometry(ldt('linear_batwing'));
  const pts = new Float64Array([1.3, 0, 0, 0, 1.3, 0]), nor = new Float64Array([0, 0, 1, 0, 0, 1]);
  engine.addPhotometricLight(0, 0, 2, id, 0, 1);
  const straight = Float64Array.from(engine.evaluatePoints(pts, nor, true).lux);
  engine.clearLights();
  engine.addPhotometricLight(0, 0, 2, id, 90, 1);
  const turned = Float64Array.from(engine.evaluatePoints(pts, nor, true).lux);
  check('luminaire rotation turns the light pattern (90°: x and y swap)',
    rel(turned[1], straight[0]) < 1e-12 && rel(turned[0], straight[1]) < 1e-12 && rel(straight[1], straight[0]) > 0.1,
    `along ${straight[0].toFixed(1)}, across ${straight[1].toFixed(1)} lx`);
  engine.delete();
}

// ---- Geometry: furniture ----------------------------------------------------------------

/// Möller-Trumbore, two-sided; true if the ray hits any triangle with t > 1e-9.
function rayHits(o, d, tris) {
  for (let k = 0; k < tris.length; k += 9) {
    const e1 = [tris[k + 3] - tris[k], tris[k + 4] - tris[k + 1], tris[k + 5] - tris[k + 2]];
    const e2 = [tris[k + 6] - tris[k], tris[k + 7] - tris[k + 1], tris[k + 8] - tris[k + 2]];
    const p = [d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]];
    const det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (Math.abs(det) < 1e-14) continue;
    const s = [o[0] - tris[k], o[1] - tris[k + 1], o[2] - tris[k + 2]];
    const u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) / det;
    if (u < 0 || u > 1) continue;
    const q = [s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]];
    const v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) / det;
    if (v < 0 || u + v > 1) continue;
    if ((e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) / det > 1e-9) return true;
  }
  return false;
}

{
  // The rule behind web/furniture.js: a work-plane point must never be enclosed by an item's
  // shadow triangles (it would read 0 lx). Enclosed = blocked upward and in all four horizontal
  // directions. Checked on a 1 cm grid at 0.75 m (+1 µm, the engine's shadow-ray bias).
  for (const type of Object.keys(CATALOG)) {
    if (CATALOG[type].solid) continue; // solids: points inside are excluded by the engine
    const g = itemGeometry(type);
    const [x0, y0, , x1, y1] = g.bounds;
    let enclosed = null, tested = 0;
    for (let x = x0; x <= x1 && !enclosed; x += 0.01) {
      for (let y = y0; y <= y1 && !enclosed; y += 0.01) {
        const o = [x, y, 0.75 + 1e-6];
        ++tested;
        if ([[1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0], [0, 0, 1]].every((d) => rayHits(o, d, g.shadowPositions))) {
          enclosed = `(${x.toFixed(2)}, ${y.toFixed(2)})`;
        }
      }
    }
    check(`furniture rule: no enclosed work-plane point in '${type}'`, !enclosed,
      enclosed ? `enclosed at ${enclosed}` : `${tested} points, ${g.shadowPositions.length / 9} shadow triangles`);
  }
}
{
  let ok = true;
  for (const [type, def] of Object.entries(CATALOG)) {
    if (!def.solid) continue;
    for (const rot of [0, 90, 180, 270]) {
      const [a0, b0, c0, a1, b1, c1] = def.solid;
      const corners = [];
      for (const x of [a0, a1]) for (const y of [b0, b1]) for (const z of [c0, c1]) corners.push(x, y, z);
      const placed = placePoints(Float64Array.from(corners), 2, 3, rot);
      const xs = [], ys = [];
      for (let k = 0; k < placed.length; k += 3) { xs.push(placed[k]); ys.push(placed[k + 1]); }
      const expected = [Math.min(...xs), Math.min(...ys), c0, Math.max(...xs), Math.max(...ys), c1];
      const got = placedSolid(def.solid, 2, 3, rot);
      if (got.some((v, k) => Math.abs(v - expected[k]) > 1e-12)) { ok = false; console.log(`   ${type} @ ${rot}°: ${got} vs ${expected}`); }
    }
  }
  check('solid furniture boxes rotate with the item (0/90/180/270°)', ok);
}

// ---- Geometry: surface helpers ----------------------------------------------------------
{
  const area = (t) => {
    let s = 0;
    for (let k = 0; k < t.length; k += 9) {
      const ux = t[k + 3] - t[k], uy = t[k + 4] - t[k + 1], uz = t[k + 5] - t[k + 2];
      const vx = t[k + 6] - t[k], vy = t[k + 7] - t[k + 1], vz = t[k + 8] - t[k + 2];
      s += Math.hypot(uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx) / 2;
    }
    return s;
  };
  const tri = Float64Array.from([0, 0, 0.7, 1.6, 0, 0.7, 1.6, 0.8, 0.7, 0, 0, 0.7, 1.6, 0.8, 0.7, 0, 0.8, 0.7]);
  const tess = tessellate(tri, 0.15, 9, [0xaa, 0xbb]);
  const normals = flatNormals(tess);
  let longest = 0, upward = true;
  for (let k = 0; k < tess.length; k += 9) {
    for (const [a, b] of [[0, 3], [3, 6], [6, 0]]) {
      longest = Math.max(longest, Math.hypot(tess[k + a] - tess[k + b], tess[k + a + 1] - tess[k + b + 1], tess[k + a + 2] - tess[k + b + 2]));
    }
    upward = upward && Math.abs(normals[k + 2] - 1) < 1e-12;
  }
  check('tessellation keeps area, winding and colours; edges ≤ 15 cm',
    rel(area(tess), area(tri)) < 1e-12 && longest <= 0.15 + 1e-12 && upward &&
    tessellate.lastColors.length === tess.length / 9 && new Set(tessellate.lastColors).size === 2,
    `${tri.length / 9} -> ${tess.length / 9} triangles, longest edge ${longest.toFixed(3)} m`);

  const u = uniqueVertices(tess, normals);
  let mapped = true;
  for (let v = 0; v < u.index.length; ++v) {
    const k = u.index[v] * 3;
    mapped = mapped && Math.abs(u.points[k] - tess[v * 3]) < 1e-4 && Math.abs(u.points[k + 2] - tess[v * 3 + 2]) < 1e-4;
  }
  check('shared vertices are evaluated once, and every vertex maps to its own position',
    mapped && u.points.length / 3 < tess.length / 3 / 3, `${tess.length / 3} vertices -> ${u.points.length / 3} evaluations`);

  const p = evaluationPoints(Float64Array.from([0, 0, 0, 0.5, 0.5, 0.5]), Float64Array.from([0, 0, 1, 1, 0, 0]), [0, 0, 0, 1, 1, 1]);
  check('evaluation points: 2 mm off the surface; points inside a solid move to its shell',
    Math.abs(p[2] - 0.002) < 1e-15 && Math.abs(p[3] - 1.002) < 1e-12 && p[4] === 0.5, `${Array.from(p).map((v) => v.toFixed(3))}`);
}

// ---- Scenes: regression values ----------------------------------------------------------
{
  // The scenes must be valid as written: the page would otherwise move items into the room on
  // load, and the page would no longer show the scene these checks compute.
  const outside = [];
  for (const scene of [referenceScene(), furnishedScene()]) {
    for (const item of scene.furniture) {
      const f = footprint(itemGeometry(item.type).bounds, item.rotation);
      if (item.x + f.minX < 0 || item.x + f.maxX > scene.room.width || item.y + f.minY < 0 || item.y + f.maxY > scene.room.depth) {
        outside.push(`${item.type} at (${item.x}, ${item.y})`);
      }
    }
  }
  check('every furniture item of the built-in scenes lies inside its room', outside.length === 0, outside.join(', '));
}
function runScene(scene, ids) {
  const engine = new lc.Engine();
  const { width, depth, height } = scene.room;
  engine.setRoom(width, depth, height);
  for (const o of scene.obstacles) {
    if (o.kind === 'box') engine.addBox(o.x0, o.y0, 0, o.x1, o.y1, o.h);
    else engine.addSphere(o.x, o.y, o.z, o.r);
  }
  for (const item of scene.furniture) {
    const g = furnitureForEngine(item);
    if (g.box) engine.addBox(...g.box); else engine.addTriangles(g.triangles);
  }
  for (const l of scene.lamps) {
    if (l.lum < 0) engine.addLight(l.x, l.y, l.z, l.cd);
    else engine.addPhotometricLight(l.x, l.y, l.z, l.lum, l.rotation, l.dimming);
  }
  const grid = { x0: 0.5, y0: 0.5, x1: width - 0.5, y1: depth - 0.5, nx: Math.round((width - 1) / 0.05), ny: Math.round((depth - 1) / 0.05) };
  const r = engine.simulate(grid.x0, grid.y0, grid.x1, grid.y1, 0.75, grid.nx, grid.ny, true);
  const out = { ...r, lux: Float64Array.from(r.lux), grid, triangles: r.triangles };
  engine.delete();
  return out;
}
{
  const ref = runScene(referenceScene());
  check('reference office: E_avg = 414.379 lx (the native C++ value)', ref.ok && ref.avgLux.toFixed(3) === '414.379', ref.avgLux?.toFixed(3));

  const probe = new lc.Engine();
  const ids = { linear: probe.addPhotometry(ldt('linear_batwing')), down: probe.addPhotometry(ldt('downlight_wide')) };
  probe.delete();
  // Photometry ids are per engine: runScene creates a fresh engine, so re-register in the same order.
  const scene = furnishedScene(ids);
  const engine2 = (() => {
    const e = new lc.Engine();
    e.addPhotometry(ldt('linear_batwing'));
    e.addPhotometry(ldt('downlight_wide'));
    return e;
  })();
  const { width, depth, height } = scene.room;
  engine2.setRoom(width, depth, height);
  for (const item of scene.furniture) {
    const g = furnitureForEngine(item);
    if (g.box) engine2.addBox(...g.box); else engine2.addTriangles(g.triangles);
  }
  for (const l of scene.lamps) engine2.addPhotometricLight(l.x, l.y, l.z, l.lum, l.rotation, l.dimming);
  const grid = { x0: 0.5, y0: 0.5, x1: width - 0.5, y1: depth - 0.5, nx: 140, ny: 100 };
  const r = engine2.simulate(grid.x0, grid.y0, grid.x1, grid.y1, 0.75, grid.nx, grid.ny, true);
  const lux = Float64Array.from(r.lux);
  const task = taskStats(lux, grid, scene.furniture);
  check('furnished office: whole plane E_avg = 527.75 lx (regression value)', Math.abs(r.avgLux - 527.754) < 0.01, r.avgLux.toFixed(3));
  check('furnished office: task areas E_avg 564, E_min 299, U0 0.53',
    task && Math.round(task.avg) === 564 && Math.round(task.min) === 299 && task.u0.toFixed(2) === '0.53',
    task ? `${task.avg.toFixed(1)} / ${task.min.toFixed(1)} / ${task.u0.toFixed(3)} over ${task.n} points` : 'none');

  // Every dark work-plane point must be explained, either as the strip up to 8 cm behind a chair
  // backrest, where all luminaires can be hidden (direct light, point sources), or as a point
  // inside a plant's foliage (within 0.35 m of its stem, where leaves overlap above it).
  // Chairs: one per workstation (local offset (0, -0.62)), four around the meeting table (0.85 m).
  const chairs = [];
  const plants = scene.furniture.filter((item) => item.type === 'plant');
  for (const item of scene.furniture) {
    const place = (localY, angleDeg) => {
      const a = (angleDeg * Math.PI) / 180;
      chairs.push({ x: item.x - Math.sin(a) * localY, y: item.y + Math.cos(a) * localY, a });
    };
    if (item.type === 'workstation') place(-0.62, item.rotation);
    if (item.type === 'meeting') for (let k = 0; k < 4; ++k) place(-0.85, item.rotation + 90 * k);
  }
  const behindABackrest = (x, y) => chairs.some((c) => {
    const dx = x - c.x, dy = y - c.y;
    const lx = Math.cos(c.a) * dx + Math.sin(c.a) * dy, ly = -Math.sin(c.a) * dx + Math.cos(c.a) * dy;
    return Math.abs(lx) <= 0.25 && ly <= -0.285 && ly >= -0.285 - 0.08;
  });
  const dark = [];
  for (let j = 0; j < grid.ny; ++j) {
    for (let i = 0; i < grid.nx; ++i) {
      if (lux[j * grid.nx + i] < 1) dark.push([grid.x0 + (i + 0.5) * 0.05, grid.y0 + (j + 0.5) * 0.05]);
    }
  }
  const inFoliage = (x, y) => plants.some((p) => Math.hypot(x - p.x, y - p.y) <= 0.35);
  const unexplained = dark.filter(([x, y]) => !behindABackrest(x, y) && !inFoliage(x, y));
  const behind = dark.filter(([x, y]) => behindABackrest(x, y)).length;
  check('furnished office: every dark point is behind a chair backrest or inside a plant',
    dark.length > 0 && unexplained.length === 0,
    `${dark.length} points below 1 lx: ${behind} behind backrests, ${dark.length - behind - unexplained.length} in foliage, ` +
    `${unexplained.length} unexplained` +
    (unexplained.length ? `: ${unexplained.slice(0, 5).map(([x, y]) => `(${x.toFixed(3)}, ${y.toFixed(3)})`).join(' ')}` : ''));
  engine2.delete();
}

if (failures > 0) {
  console.error(`${failures} check(s) failed`);
  process.exit(1);
}
console.log('all web checks passed');

// Scene definitions and task-area evaluation for LumaCore Web.
//
// Pure functions without DOM or three.js, shared by app.js and the Node.js checks in
// tools/web_check.mjs, so the page and its tests use exactly the same scenes and rules.

import { CATALOG, itemGeometry, placePoints, placedSolid } from './furniture.js';

export const ISOTROPIC = -1; // luminaire id of an isotropic point source

export const newLamp = (x, y, extra = {}) =>
  ({ x, y, z: 2.8, lum: ISOTROPIC, cd: 800, dimming: 1, rotation: 0, ...extra });

/// The office of native/apps/common/scenarios.cpp: results match the C++ engine bit for bit.
/// Boxes keep their exact corners (x0..x1, y0..y1, height h) for that reason.
export function referenceScene() {
  return {
    room: { width: 6, depth: 4, height: 3 },
    lamps: [[1, 1], [3, 1], [5, 1], [1, 3], [3, 3], [5, 3]].map(([x, y]) => newLamp(x, y)),
    obstacles: [
      { kind: 'box', x0: 0.05, y0: 2.6, x1: 0.65, y1: 3.95, h: 2.0 }, // cabinet
      { kind: 'box', x0: 2.9, y0: 0.2, x1: 3.1, y1: 1.6, h: 1.6 },    // partition
      { kind: 'sphere', x: 4.5, y: 2.6, z: 1.9, r: 0.25 },            // pendant globe
    ],
    furniture: [],
  };
}

/// An 8 x 6 m open-plan office: 4 x 3 linear luminaires (about 44,000 lm for 48 m²; 500 lx needs
/// roughly that with direct light only), two dimmed downlights over the meeting table, four
/// workstations, partitions, shelves, a cabinet and plants. The desk rows of luminaires hang over
/// the working zones, on the user's side of the monitors: luminaires behind a screen are blocked
/// by it (this layout was chosen by comparing alternatives on the task-area values).
/// `lum` gives the photometry ids of the 'linear' and 'down' luminaires.
export function furnishedScene(lum = { linear: ISOTROPIC, down: ISOTROPIC }) {
  const linear = { lum: lum.linear, dimming: 1.15 };
  const down = { lum: lum.down, dimming: 0.6 };
  const grid = [];
  for (const y of [1.2, 3.0, 4.8]) for (const x of [0.9, 2.55, 4.2, 7.2]) grid.push(newLamp(x, y, linear));
  return {
    room: { width: 8, depth: 6, height: 3 },
    lamps: [...grid, newLamp(6.6, 2.4, down), newLamp(6.6, 3.6, down)],
    obstacles: [],
    furniture: [
      { type: 'workstation', x: 1.6, y: 1.6, rotation: 0 },
      { type: 'workstation', x: 3.5, y: 1.6, rotation: 0 },
      { type: 'workstation', x: 1.6, y: 4.4, rotation: 180 },
      { type: 'workstation', x: 3.5, y: 4.4, rotation: 180 },
      { type: 'partition', x: 1.6, y: 3.0, rotation: 0 },
      { type: 'partition', x: 3.5, y: 3.0, rotation: 0 },
      { type: 'meeting', x: 6.6, y: 3.0, rotation: 0 },
      { type: 'bookshelf', x: 5.2, y: 5.8, rotation: 0 }, // back against the wall, books facing the room
      { type: 'bookshelf', x: 6.2, y: 5.8, rotation: 0 },
      { type: 'cabinet', x: 0.26, y: 3.0, rotation: 90 },
      { type: 'plant', x: 0.6, y: 0.6, rotation: 0 },   // leaves reach about 0.55 m: kept off the walls
      { type: 'plant', x: 7.1, y: 0.8, rotation: 40 },
      { type: 'plant', x: 4.55, y: 3.0, rotation: 80 },
    ],
  };
}

export const isSolidItem = (item) => item.type !== 'model' && !!CATALOG[item.type]?.solid;

/// Footprint of an item with local bounds [x0, y0, z0, x1, y1, z1] after rotation about z,
/// relative to its position: { minX, maxX, minY, maxY }.
export function footprint(bounds, rotationDeg) {
  const [x0, y0, , x1, y1] = bounds;
  const a = (rotationDeg * Math.PI) / 180, c = Math.cos(a), s = Math.sin(a);
  const xs = [], ys = [];
  for (const [x, y] of [[x0, y0], [x1, y0], [x1, y1], [x0, y1]]) {
    xs.push(c * x - s * y);
    ys.push(s * x + c * y);
  }
  return { minX: Math.min(...xs), maxX: Math.max(...xs), minY: Math.min(...ys), maxY: Math.max(...ys) };
}

/// What the engine receives for a catalogue item: closed furniture as a solid axis-aligned box
/// { box: [x0, y0, z0, x1, y1, z1] }, everything else as shadow triangles { triangles }.
export function furnitureForEngine(item) {
  if (isSolidItem(item)) return { box: placedSolid(CATALOG[item.type].solid, item.x, item.y, item.rotation) };
  return { triangles: placePoints(itemGeometry(item.type).shadowPositions, item.x, item.y, item.rotation) };
}

// ---- Task areas ---------------------------------------------------------------------------
// The surfaces where the visual task happens. EN 12464-1 sets its office requirements on the task
// area, not on every point of the room: a direct-only point-source model leaves small dark pockets
// right behind chair backs, which would otherwise make the room's E_min 0 lx.

export const TASK_TARGET = { lux: 500, u0: 0.6 }; // EN 12464-1 office tasks (writing, typing, reading)

/// Workstations: the working zone in front of the monitor (full desk width, from the front edge
/// to the monitor foot); plain desks: the whole top; the meeting table: its round top.
export function taskAreas(furniture) {
  const areas = [];
  for (const item of furniture) {
    if (item.type === 'workstation') areas.push({ ...item, x0: -0.8, x1: 0.8, y0: -0.4, y1: 0.1 });
    if (item.type === 'desk') areas.push({ ...item, x0: -0.8, x1: 0.8, y0: -0.4, y1: 0.4 });
    if (item.type === 'meeting') areas.push({ ...item, r: 0.6 });
  }
  return areas;
}

export function inTaskArea(area, x, y) {
  const dx = x - area.x, dy = y - area.y;
  if (area.r) return dx * dx + dy * dy <= area.r * area.r;
  const a = (area.rotation * Math.PI) / 180, c = Math.cos(a), s = Math.sin(a);
  const lx = c * dx + s * dy, ly = -s * dx + c * dy; // into the item's frame
  return lx >= area.x0 && lx <= area.x1 && ly >= area.y0 && ly <= area.y1;
}

/// E_avg, E_min, E_max and U0 over the work-plane grid points inside the task areas, or null.
/// `grid` = { x0, y0, x1, y1, nx, ny }, `lux` row-major (j * nx + i), NaN = not measured.
export function taskStats(lux, grid, furniture) {
  const areas = taskAreas(furniture);
  if (!areas.length) return null;
  const dx = (grid.x1 - grid.x0) / grid.nx, dy = (grid.y1 - grid.y0) / grid.ny;
  let min = Infinity, max = 0, sum = 0, n = 0;
  for (let j = 0; j < grid.ny; ++j) {
    const y = grid.y0 + (j + 0.5) * dy;
    for (let i = 0; i < grid.nx; ++i) {
      const x = grid.x0 + (i + 0.5) * dx;
      const e = lux[j * grid.nx + i];
      if (Number.isNaN(e) || !areas.some((a) => inTaskArea(a, x, y))) continue;
      min = Math.min(min, e);
      max = Math.max(max, e);
      sum += e;
      ++n;
    }
  }
  if (!n) return null;
  const avg = sum / n;
  return { avg, min, max, u0: avg > 0 ? min / avg : 0, n, areas: areas.length };
}

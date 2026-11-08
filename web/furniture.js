// Procedural furniture for LumaCore Web.
//
// Every item is a list of triangles in item-local metres (origin = centre of the footprint on
// the floor, +z up), each with an sRGB colour. The same triangles are drawn by three.js and sent
// to the C++ engine as shadow-casting geometry.
//
// Physics rule: nothing hollow may straddle the work plane (default 0.75 m). A measurement
// point inside a closed shape cannot see any lamp and would read 0 lx. So desk tops end exactly
// at 0.75 m (the work plane is the desk surface, as in lighting design practice), thin parts
// that must cross it are zero-thickness panels, and closed furniture (bookshelf, cabinet,
// partition) is given to the engine as a solid box (`solid`), which the engine excludes from
// the measurement like any other solid obstacle. Solid items rotate in 90° steps, because
// engine boxes are axis-aligned.

// Colours (sRGB).
const OAK = 0xc8a07a, WHITE = 0xecebe6, METAL = 0x9a9da2, DARK = 0x3b3e43, BLACK = 0x1c1e21;
const FABRIC = 0x34465c, FELT = 0x7f8fa0, TERRACOTTA = 0xb4643c, SOIL = 0x3e2c20;
const LEAVES = [0x3f7d3a, 0x4f9446, 0x2f6a2e, 0x5b9e4f];
const BOOKS = [0x8c2f39, 0x2f4b7c, 0xd9a441, 0x3b7a57, 0x6b4c8a, 0xc8553d, 0x2a2a2a, 0xe5e1d8, 0x4c7d99];

/// Small deterministic random generator, so the same item always looks the same.
function mulberry32(seed) {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

/// Collects triangles under a stack of 2D transforms (rotation about z, then translation).
class Builder {
  constructor() {
    this.pos = [];        // drawn triangles (9 numbers each)
    this.col = [];        // one sRGB colour per drawn triangle
    this.shadowPos = [];  // triangles sent to the engine
    this.draw = true;
    this.shadows = true;
    this.stack = [[1, 0, 0, 0]]; // [cos, sin, tx, ty]
  }

  /// Parts drawn but not sent to the engine: things lying on the work plane itself (keyboard,
  /// mouse, monitor foot). The plane is measured at their bottom face, where a shadow ray would
  /// start inside them; their real effect is the dark spot right underneath, which is not part of
  /// the task area.
  displayOnly(fn) {
    this.shadows = false;
    fn();
    this.shadows = true;
  }

  /// Parts sent to the engine but not drawn: the stand-in for a closed part that straddles the
  /// work plane, e.g. a chair backrest represented by its mid-plane. A thin part casts almost
  /// the same shadow as its mid-plane, but a plane has no inside and no underside, so no measured
  /// point can be trapped in a pocket where every lamp is blocked.
  shadowOnly(fn) {
    this.draw = false;
    fn();
    this.draw = true;
  }

  push(angleRad, tx, ty) {
    const [c, s, x, y] = this.stack[this.stack.length - 1];
    const c2 = Math.cos(angleRad), s2 = Math.sin(angleRad);
    // current ∘ (rotate then translate)
    this.stack.push([c * c2 - s * s2, s * c2 + c * s2, x + c * tx - s * ty, y + s * tx + c * ty]);
  }

  pop() { this.stack.pop(); }

  p(x, y, z) {
    const [c, s, tx, ty] = this.stack[this.stack.length - 1];
    return [c * x - s * y + tx, s * x + c * y + ty, z];
  }

  tri(a, b, c, color) {
    if (this.draw) {
      this.pos.push(...a, ...b, ...c);
      this.col.push(color);
    }
    if (this.shadows) this.shadowPos.push(...a, ...b, ...c);
  }

  quad(a, b, c, d, color) {
    this.tri(a, b, c, color);
    this.tri(a, c, d, color);
  }

  /// Axis-aligned box in the current frame, counter-clockwise faces seen from outside.
  box(x0, y0, z0, x1, y1, z1, color) {
    const P = (x, y, z) => this.p(x, y, z);
    const faces = [
      [[x0, y0, z0], [x0, y0, z1], [x0, y1, z1], [x0, y1, z0]],
      [[x1, y0, z0], [x1, y1, z0], [x1, y1, z1], [x1, y0, z1]],
      [[x0, y0, z0], [x1, y0, z0], [x1, y0, z1], [x0, y0, z1]],
      [[x0, y1, z0], [x0, y1, z1], [x1, y1, z1], [x1, y1, z0]],
      [[x0, y0, z0], [x0, y1, z0], [x1, y1, z0], [x1, y0, z0]],
      [[x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1]],
    ];
    for (const f of faces) this.quad(P(...f[0]), P(...f[1]), P(...f[2]), P(...f[3]), color);
  }

  /// Frustum (r0 at z0, r1 at z1) with caps; outward winding.
  cylinder(cx, cy, z0, z1, r0, r1, color, segments = 20) {
    const ring = (r, z, k) => {
      const a = (2 * Math.PI * k) / segments;
      return this.p(cx + r * Math.cos(a), cy + r * Math.sin(a), z);
    };
    const bottom = this.p(cx, cy, z0), top = this.p(cx, cy, z1);
    for (let k = 0; k < segments; ++k) {
      const b0 = ring(r0, z0, k), b1 = ring(r0, z0, k + 1), t0 = ring(r1, z1, k), t1 = ring(r1, z1, k + 1);
      this.quad(b0, b1, t1, t0, color);
      this.tri(top, t0, t1, color);
      this.tri(bottom, b1, b0, color);
    }
  }

  /// A vertical zero-thickness panel (two-sided for the engine; no inside).
  panel(x0, y0, x1, y1, z0, z1, color) {
    this.quad(this.p(x0, y0, z0), this.p(x1, y1, z0), this.p(x1, y1, z1), this.p(x0, y0, z1), color);
  }
}

// ---- Pieces -----------------------------------------------------------------------------

function desk(b) {
  b.box(-0.8, -0.4, 0.72, 0.8, 0.4, 0.75, OAK); // top surface exactly at the work plane
  for (const sx of [-1, 1]) {
    for (const sy of [-1, 1]) {
      b.box(sx * 0.74 - 0.025, sy * 0.34 - 0.025, 0, sx * 0.74 + 0.025, sy * 0.34 + 0.025, 0.72, METAL);
    }
  }
  b.box(-0.7, 0.33, 0.3, 0.7, 0.35, 0.68, METAL); // modesty panel
}

function monitorAndKeyboard(b) {
  b.displayOnly(() => {
    b.box(-0.12, 0.1, 0.75, 0.12, 0.24, 0.765, DARK);  // monitor foot (on the desk)
    b.box(-0.22, -0.22, 0.75, 0.22, -0.08, 0.77, DARK); // keyboard
    b.box(0.3, -0.2, 0.75, 0.36, -0.1, 0.78, DARK);     // mouse
  });
  b.box(-0.025, 0.18, 0.765, 0.025, 0.21, 0.95, DARK); // neck: above the plane, casts shadows
  b.box(-0.3, 0.13, 0.9, 0.3, 0.16, 1.24, BLACK);      // screen
}

/// Office chair facing +y, centred on the origin.
function chair(b) {
  b.box(-0.25, -0.24, 0.44, 0.25, 0.24, 0.51, FABRIC);      // seat
  // Backrest: drawn as a cushion, sent to the engine as its mid-plane (it straddles 0.75 m).
  b.displayOnly(() => b.box(-0.23, -0.31, 0.56, 0.23, -0.26, 1.05, FABRIC));
  b.shadowOnly(() => b.panel(-0.23, -0.285, 0.23, -0.285, 0.56, 1.05, FABRIC));
  b.box(-0.03, -0.3, 0.46, 0.03, -0.27, 0.56, METAL);      // back support, below the plane
  for (const sx of [-1, 1]) {
    b.box(sx * 0.29 - 0.02, -0.12, 0.62, sx * 0.29 + 0.02, 0.14, 0.66, DARK); // armrest
    b.box(sx * 0.29 - 0.01, 0.0, 0.51, sx * 0.29 + 0.01, 0.02, 0.62, DARK);
  }
  b.cylinder(0, 0, 0.12, 0.44, 0.028, 0.028, METAL, 12);   // gas column
  b.cylinder(0, 0, 0.07, 0.12, 0.06, 0.06, DARK, 12);       // hub
  for (let k = 0; k < 5; ++k) {                             // five-star base with castors
    b.push((2 * Math.PI * k) / 5, 0, 0);
    b.box(0, -0.02, 0.07, 0.3, 0.02, 0.11, DARK);
    b.cylinder(0.28, 0, 0, 0.06, 0.03, 0.03, BLACK, 10);
    b.pop();
  }
}

function bookshelf(b, rand) {
  b.box(-0.45, -0.175, 0, -0.43, 0.175, 1.9, WHITE);
  b.box(0.43, -0.175, 0, 0.45, 0.175, 1.9, WHITE);
  b.box(-0.43, -0.175, 1.88, 0.43, 0.175, 1.9, WHITE);
  b.box(-0.43, -0.175, 0, 0.43, 0.175, 0.08, WHITE);
  b.box(-0.43, 0.155, 0.08, 0.43, 0.175, 1.88, WHITE);
  const levels = [0.08, 0.53, 0.98, 1.43];
  for (const z of levels.slice(1)) b.box(-0.43, -0.175, z - 0.02, 0.43, 0.155, z, WHITE);
  for (const z of levels) {
    let x = -0.42;
    while (x < 0.39) {
      const w = 0.02 + rand() * 0.035;
      if (rand() < 0.08) { x += w * 2; continue; } // a gap
      if (x + w > 0.42) break;
      const h = 0.18 + rand() * 0.14, d = 0.19 + rand() * 0.07;
      b.box(x, -0.15, z, x + w, -0.15 + d, z + h, BOOKS[Math.floor(rand() * BOOKS.length)]);
      x += w + 0.002;
    }
  }
}

function plant(b, rand) {
  b.cylinder(0, 0, 0, 0.42, 0.16, 0.21, TERRACOTTA, 24); // pot, below the work plane
  b.cylinder(0, 0, 0.36, 0.38, 0.2, 0.2, SOIL, 24);
  const golden = Math.PI * (3 - Math.sqrt(5));
  for (let k = 0; k < 18; ++k) {
    // Leaves are zero-thickness blades: they shade the work plane but have no inside.
    const a = k * golden, e = (35 + rand() * 45) * Math.PI / 180;
    const len = 0.45 + rand() * 0.45, w = 0.06 + rand() * 0.05;
    const dir = [Math.cos(e) * Math.cos(a), Math.cos(e) * Math.sin(a), Math.sin(e)];
    const side = [-Math.sin(a) * w / 2, Math.cos(a) * w / 2, 0];
    const base = [0.02 * Math.cos(a), 0.02 * Math.sin(a), 0.38];
    const mid = [base[0] + dir[0] * len * 0.55, base[1] + dir[1] * len * 0.55, base[2] + dir[2] * len * 0.55];
    const tip = [base[0] + dir[0] * len, base[1] + dir[1] * len, base[2] + dir[2] * len * 0.75]; // droops
    const color = LEAVES[k % LEAVES.length];
    b.quad(b.p(...base), b.p(mid[0] - side[0], mid[1] - side[1], mid[2]), b.p(...tip),
      b.p(mid[0] + side[0], mid[1] + side[1], mid[2]), color);
  }
}

function meeting(b) {
  b.cylinder(0, 0, 0.72, 0.75, 0.6, 0.6, WHITE, 40);  // top at the work plane
  b.cylinder(0, 0, 0.03, 0.72, 0.05, 0.05, METAL, 16);
  b.cylinder(0, 0, 0, 0.03, 0.32, 0.32, DARK, 28);
  for (let k = 0; k < 4; ++k) {
    b.push((Math.PI / 2) * k, 0, 0);
    b.push(0, 0, -0.85); // chair facing the table centre
    chair(b);
    b.pop();
    b.pop();
  }
}

function partition(b) {
  b.box(-0.8, -0.025, 0.05, 0.8, 0.025, 1.4, FELT);
  for (const sx of [-1, 1]) b.box(sx * 0.72 - 0.03, -0.2, 0, sx * 0.72 + 0.03, 0.2, 0.05, METAL);
}

function cabinet(b) {
  b.box(-0.4, -0.225, 0, 0.4, 0.225, 2.0, WHITE);
  b.box(-0.004, -0.228, 0.04, 0.004, -0.225, 1.96, DARK); // door gap
  for (const sx of [-1, 1]) b.box(sx * 0.05 - 0.008, -0.25, 0.95, sx * 0.05 + 0.008, -0.225, 1.15, METAL);
}

// ---- Catalogue --------------------------------------------------------------------------

export const CATALOG = {
  workstation: {
    label: 'Workstation (desk, monitor, chair)',
    build: (b) => { desk(b); monitorAndKeyboard(b); b.push(0, 0, -0.62); chair(b); b.pop(); },
  },
  desk: { label: 'Desk 160 × 80 cm', build: (b) => desk(b) },
  chair: { label: 'Office chair', build: (b) => chair(b) },
  meeting: { label: 'Meeting table with 4 chairs', build: (b) => meeting(b) },
  bookshelf: { label: 'Bookshelf with books', build: bookshelf, solid: [-0.45, -0.175, 0, 0.45, 0.175, 1.9] },
  cabinet: { label: 'Tall storage cabinet', build: (b) => cabinet(b), solid: [-0.4, -0.25, 0, 0.4, 0.225, 2.0] },
  partition: { label: 'Acoustic partition screen', build: (b) => partition(b), solid: [-0.8, -0.025, 0.05, 0.8, 0.025, 1.4] },
  plant: { label: 'Plant in a pot', build: plant },
};

const cache = new Map();

/// Local geometry of a catalogue item: { positions: Float64Array (9 per triangle, all drawn),
/// shadowPositions: Float64Array (the triangles that cast shadows), colors: number[] (one sRGB
/// colour per triangle), bounds: [x0, y0, z0, x1, y1, z1], solid }.
export function itemGeometry(type) {
  if (cache.has(type)) return cache.get(type);
  const def = CATALOG[type];
  if (!def) throw new Error(`unknown furniture type '${type}'`);
  const b = new Builder();
  def.build(b, mulberry32(0x5eed + type.length * 7919));
  const positions = Float64Array.from(b.pos);
  const bounds = [Infinity, Infinity, Infinity, -Infinity, -Infinity, -Infinity];
  for (let k = 0; k < positions.length; k += 3) {
    for (let a = 0; a < 3; ++a) {
      bounds[a] = Math.min(bounds[a], positions[k + a]);
      bounds[a + 3] = Math.max(bounds[a + 3], positions[k + a]);
    }
  }
  const geometry = { positions, shadowPositions: Float64Array.from(b.shadowPos), colors: b.col, bounds, solid: def.solid ?? null };
  cache.set(type, geometry);
  return geometry;
}

/// Applies an item's placement (rotation about z in degrees, then translation) to local points.
export function placePoints(local, x, y, rotationDeg) {
  const a = (rotationDeg * Math.PI) / 180, c = Math.cos(a), s = Math.sin(a);
  const out = new Float64Array(local.length);
  for (let k = 0; k < local.length; k += 3) {
    out[k] = c * local[k] - s * local[k + 1] + x;
    out[k + 1] = s * local[k] + c * local[k + 1] + y;
    out[k + 2] = local[k + 2];
  }
  return out;
}

/// World-space axis-aligned solid box of a solid item (rotation must be a multiple of 90°).
export function placedSolid(solid, x, y, rotationDeg) {
  const quarter = ((Math.round(rotationDeg / 90) % 4) + 4) % 4;
  let [x0, y0, z0, x1, y1, z1] = solid;
  for (let k = 0; k < quarter; ++k) [x0, y0, x1, y1] = [-y1, x0, -y0, x1]; // rotate +90°
  return [x0 + x, y0 + y, z0, x1 + x, y1 + y, z1];
}

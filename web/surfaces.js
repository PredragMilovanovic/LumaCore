// Geometry helpers for "light on all surfaces": tessellation and evaluation points.
//
// The C++ engine evaluates direct illuminance at any point with any surface direction. To colour
// furniture by its computed light, large faces (a 1.6 m desk top is two triangles) are split
// into small triangles, so shadows across them become visible, and each vertex gets its own
// evaluation.

/// Splits triangles until no edge is longer than maxEdge (longest-edge bisection, depth-capped).
/// Input and output: flat non-indexed [ax, ay, az, bx, by, bz, cx, cy, cz, ...]. If `colors` (one
/// per input triangle) is given, `tessellate.lastColors` holds one per output triangle.
export function tessellate(positions, maxEdge, maxDepth = 9, colors = null) {
  const out = [];
  const outColors = [];
  let current = 0;
  const max2 = maxEdge * maxEdge;
  const d2 = (p, q) => (p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 + (p[2] - q[2]) ** 2;
  const mid = (p, q) => [(p[0] + q[0]) / 2, (p[1] + q[1]) / 2, (p[2] + q[2]) / 2];
  const split = (a, b, c, depth) => {
    const ab = d2(a, b), bc = d2(b, c), ca = d2(c, a);
    const longest = Math.max(ab, bc, ca);
    if (longest <= max2 || depth >= maxDepth) {
      out.push(...a, ...b, ...c);
      if (colors) outColors.push(colors[current]);
      return;
    }
    // Bisect the longest edge; both halves keep the original winding.
    if (longest === ab) { const m = mid(a, b); split(a, m, c, depth + 1); split(m, b, c, depth + 1); }
    else if (longest === bc) { const m = mid(b, c); split(a, b, m, depth + 1); split(a, m, c, depth + 1); }
    else { const m = mid(c, a); split(a, b, m, depth + 1); split(m, b, c, depth + 1); }
  };
  for (let k = 0; k + 8 < positions.length; k += 9) {
    current = k / 9;
    split([positions[k], positions[k + 1], positions[k + 2]],
      [positions[k + 3], positions[k + 4], positions[k + 5]],
      [positions[k + 6], positions[k + 7], positions[k + 8]], 0);
  }
  tessellate.lastColors = outColors;
  return Float64Array.from(out);
}

/// Merges vertices with the same position and normal (to 0.1 mm / 0.001), so a point shared by
/// several small triangles is evaluated once. Returns { points, normals } of the unique vertices
/// and `index`: for every input vertex, its unique vertex.
export function uniqueVertices(positions, normals) {
  const map = new Map();
  const points = [], uniqueNormals = [];
  const index = new Uint32Array(positions.length / 3);
  for (let v = 0, k = 0; k < positions.length; ++v, k += 3) {
    const key = `${Math.round(positions[k] * 1e4)},${Math.round(positions[k + 1] * 1e4)},${Math.round(positions[k + 2] * 1e4)},` +
      `${Math.round(normals[k] * 1e3)},${Math.round(normals[k + 1] * 1e3)},${Math.round(normals[k + 2] * 1e3)}`;
    let id = map.get(key);
    if (id === undefined) {
      id = points.length / 3;
      map.set(key, id);
      points.push(positions[k], positions[k + 1], positions[k + 2]);
      uniqueNormals.push(normals[k], normals[k + 1], normals[k + 2]);
    }
    index[v] = id;
  }
  return { points: Float64Array.from(points), normals: Float64Array.from(uniqueNormals), index };
}

/// Flat (per-face) normals, repeated for the three vertices of each triangle (unit length; zero
/// for degenerate triangles, which the engine reports as not measured).
export function flatNormals(positions) {
  const n = new Float64Array(positions.length);
  for (let k = 0; k + 8 < positions.length; k += 9) {
    const ux = positions[k + 3] - positions[k], uy = positions[k + 4] - positions[k + 1], uz = positions[k + 5] - positions[k + 2];
    const vx = positions[k + 6] - positions[k], vy = positions[k + 7] - positions[k + 1], vz = positions[k + 8] - positions[k + 2];
    let x = uy * vz - uz * vy, y = uz * vx - ux * vz, z = ux * vy - uy * vx;
    const len = Math.hypot(x, y, z);
    if (len > 0) { x /= len; y /= len; z /= len; }
    for (let v = 0; v < 3; ++v) n.set([x, y, z], k + 3 * v);
  }
  return n;
}

/// Evaluation points: each vertex moved 2 mm along its normal, so a shadow ray from a corner does
/// not graze the neighbouring face of the same object. If a solid box is given (closed furniture,
/// which the engine treats as one solid block), points inside it are moved along the normal to
/// where they leave the box: the light there is what reaches the object's outer shell.
export function evaluationPoints(positions, normals, solid = null) {
  const p = new Float64Array(positions.length);
  for (let k = 0; k < positions.length; k += 3) {
    let x = positions[k], y = positions[k + 1], z = positions[k + 2];
    const nx = normals[k], ny = normals[k + 1], nz = normals[k + 2];
    if (solid && x > solid[0] && x < solid[3] && y > solid[1] && y < solid[4] && z > solid[2] && z < solid[5]) {
      const exit = (lo, hi, o, d) => (d > 0 ? (hi - o) / d : d < 0 ? (lo - o) / d : Infinity);
      const t = Math.min(exit(solid[0], solid[3], x, nx), exit(solid[1], solid[4], y, ny), exit(solid[2], solid[5], z, nz));
      if (Number.isFinite(t)) { x += nx * t; y += ny * t; z += nz * t; }
    }
    p[k] = x + nx * 0.002;
    p[k + 1] = y + ny * 0.002;
    p[k + 2] = z + nz * 0.002;
  }
  return p;
}

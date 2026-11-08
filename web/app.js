// LumaCore Web: interactive 3D front end for the C++ engine compiled to WebAssembly.
// Coordinates are the engine's: metres, right-handed, +Z up.

import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import createLumaCore from './lumacore.js';

// ---- Defaults (same office as native/apps/common/scenarios.cpp) --------------------------

const BORDER = 0.5;          // excluded band along the walls (EN 12464-1 border zone)
const LAMP_BELOW_CEILING = 0.2;
const ISOTROPIC = -1;        // luminaire id of an isotropic point source
const SAMPLE_LDT = ['downlight_wide', 'spot_narrow', 'linear_batwing'];

const defaultRoom = () => ({ width: 6, depth: 4, height: 3 });
const newLamp = (x, y, z = 2.8) => ({ x, y, z, lum: ISOTROPIC, cd: 800, dimming: 1, rotation: 0 });
const defaultLamps = () => [[1, 1], [3, 1], [5, 1], [1, 3], [3, 3], [5, 3]].map(([x, y]) => newLamp(x, y));
// Boxes stand on the floor and keep their exact corners (x0..x1, y0..y1, height h), so the
// default office matches the C++ scenario bit for bit. Spheres: centre and radius.
const defaultObstacles = () => [
  { kind: 'box', x0: 0.05, y0: 2.6, x1: 0.65, y1: 3.95, h: 2.0 }, // cabinet
  { kind: 'box', x0: 2.9, y0: 0.2, x1: 3.1, y1: 1.6, h: 1.6 },    // partition
  { kind: 'sphere', x: 4.5, y: 2.6, z: 1.9, r: 0.25 },            // pendant globe
];
const centerOf = (o) => (o.kind === 'box' ? { x: (o.x0 + o.x1) / 2, y: (o.y0 + o.y1) / 2 } : { x: o.x, y: o.y });
const halfSize = (o) => (o.kind === 'box' ? { x: (o.x1 - o.x0) / 2, y: (o.y1 - o.y0) / 2 } : { x: o.r, y: o.r });
/// Moves an obstacle by (dx, dy); boxes shift both corners.
function shift(o, dx, dy) {
  if (o.kind === 'box') { o.x0 += dx; o.x1 += dx; o.y0 += dy; o.y1 += dy; } else { o.x += dx; o.y += dy; }
}
/// Resizes a box about its centre.
function resizeBox(o, w, d) {
  const c = centerOf(o);
  Object.assign(o, { x0: c.x - w / 2, x1: c.x + w / 2, y0: c.y - d / 2, y1: c.y + d / 2 });
}

// Sequential single-hue ramp, light (low lux) -> dark (high lux); same as tools/heatmap.py.
const RAMP = ['#cde2fb', '#b7d3f6', '#9ec5f4', '#86b6ef', '#6da7ec', '#5598e7', '#3987e5',
  '#2a78d6', '#256abf', '#1c5cab', '#184f95', '#104281', '#0d366b'];
const NOT_MEASURED = [0xc9, 0xc8, 0xc2];

const state = {
  room: defaultRoom(),
  lamps: defaultLamps(),
  obstacles: defaultObstacles(),
  sel: { kind: 'lamp', index: 0 }, // or { kind: 'obstacle', index } or null
  workplane: 0.75,
  cell: 0.05,
  shadows: true,
  scaleMax: 600, // colour scale top [lx]; grows if exceeded, never jumps while dragging
};

// ---- Helpers ------------------------------------------------------------------------------

const $ = (id) => document.getElementById(id);
const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
/// A finite number from untrusted input (link, JSON), else the fallback. Missing values must not
/// become 0: Number(null) and Number('') are 0 in JavaScript.
const num = (v, fallback) =>
  (v === null || v === undefined || v === '' || !Number.isFinite(Number(v)) ? fallback : Number(v));
const fmt = (v, d = 0) => v.toLocaleString('en-US', { minimumFractionDigits: d, maximumFractionDigits: d });
const RAMP_RGB = RAMP.map((h) => [1, 3, 5].map((k) => parseInt(h.slice(k, k + 2), 16)));

function rampColor(t) {
  const x = clamp(t, 0, 1) * (RAMP_RGB.length - 1);
  const k = Math.min(RAMP_RGB.length - 2, Math.floor(x));
  const f = x - k;
  const a = RAMP_RGB[k], b = RAMP_RGB[k + 1];
  return [0, 1, 2].map((c) => Math.round(a[c] + (b[c] - a[c]) * f));
}

function gridSpec() {
  const { width, depth } = state.room;
  const x0 = BORDER, y0 = BORDER, x1 = width - BORDER, y1 = depth - BORDER;
  return {
    x0, y0, x1, y1,
    nx: Math.max(1, Math.round((x1 - x0) / state.cell)),
    ny: Math.max(1, Math.round((y1 - y0) / state.cell)),
  };
}

const selectedLamp = () => (state.sel?.kind === 'lamp' ? state.lamps[state.sel.index] : undefined);
const selectedObstacle = () => (state.sel?.kind === 'obstacle' ? state.obstacles[state.sel.index] : undefined);

/// Keeps lamps and obstacles inside the room after it was resized (or loaded from a link).
function fitToRoom() {
  const { width, depth, height } = state.room;
  for (const l of state.lamps) {
    l.x = clamp(l.x, 0.05, width - 0.05);
    l.y = clamp(l.y, 0.05, depth - 0.05);
    l.z = height - LAMP_BELOW_CEILING;
  }
  for (const o of state.obstacles) {
    if (o.kind === 'box') {
      // Only touch the corners when the box sticks out; never re-derive them needlessly.
      if (o.x1 - o.x0 > width) resizeBox(o, width, o.y1 - o.y0);
      if (o.y1 - o.y0 > depth) resizeBox(o, o.x1 - o.x0, depth);
      if (o.x0 < 0) shift(o, -o.x0, 0);
      if (o.x1 > width) shift(o, width - o.x1, 0);
      if (o.y0 < 0) shift(o, 0, -o.y0);
      if (o.y1 > depth) shift(o, 0, depth - o.y1);
      o.h = clamp(o.h, 0.05, height);
    } else {
      o.r = clamp(o.r, 0.05, Math.min(width, depth, height) / 2);
      o.x = clamp(o.x, o.r, width - o.r);
      o.y = clamp(o.y, o.r, depth - o.r);
      o.z = clamp(o.z, o.r, height - o.r);
    }
  }
  state.workplane = clamp(state.workplane, 0, maxWorkplane());
}
const maxWorkplane = () => Math.min(1.5, state.room.height - LAMP_BELOW_CEILING - 0.3);

// ---- Engine -------------------------------------------------------------------------------

let lc, engine;
let last = null;  // latest successful run: { ..., lux: Float64Array, grid }
let notice = '';  // persistent message (e.g. an unreadable share link), shown after each run

try {
  lc = await createLumaCore();
} catch (err) {
  $('loading').textContent = `Could not load the WebAssembly engine: ${err}`;
  throw err;
}
engine = new lc.Engine();

// Luminaire catalogue: id -> { label, info, key }. key = sample name for shareable links.
const catalog = new Map([[ISOTROPIC, { label: 'Isotropic point source (bare lamp)', info: null, key: 'iso' }]]);
const idByKey = { iso: ISOTROPIC };

function addPhotometry(text, key) {
  const id = engine.addPhotometry(text);
  if (id < 0) throw new Error(engine.lastError());
  const info = engine.photometryInfo(id);
  catalog.set(id, { label: info.name, info, key });
  if (key) idByKey[key] = id;
  return id;
}

for (const key of SAMPLE_LDT) { // sequential: stable ids and menu order
  try {
    const response = await fetch(`ldt/${key}.ldt`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    addPhotometry(await response.text(), key);
  } catch (err) {
    console.warn(`sample ${key} not loaded: ${err.message}`);
  }
}

function compute() {
  const { width, depth, height } = state.room;
  engine.setRoom(width, depth, height);
  engine.clearObstacles();
  for (const o of state.obstacles) {
    if (o.kind === 'box') engine.addBox(o.x0, o.y0, 0, o.x1, o.y1, o.h);
    else engine.addSphere(o.x, o.y, o.z, o.r);
  }
  engine.clearLights();
  for (const l of state.lamps) {
    if (l.lum === ISOTROPIC) engine.addLight(l.x, l.y, l.z, l.cd);
    else engine.addPhotometricLight(l.x, l.y, l.z, l.lum, l.rotation, l.dimming);
  }
  const g = gridSpec();
  const r = engine.simulate(g.x0, g.y0, g.x1, g.y1, state.workplane, g.nx, g.ny, state.shadows);
  if (!r.ok) {
    $('error').textContent = `Engine error: ${r.error}`;
    return;
  }
  $('error').textContent = notice;
  // r.lux is a view into WebAssembly memory, valid only until the next call: copy it.
  last = { ...r, lux: Float64Array.from(r.lux), grid: g };
  if (r.maxLux > state.scaleMax) state.scaleMax = Math.ceil(r.maxLux / 100) * 100;
  paintHeatmap();
  showStats();
  document.body.dataset.avgLux = r.avgLux.toFixed(3); // for automated checks
}

let pending = false;
function scheduleCompute() {
  if (pending) return;
  pending = true;
  requestAnimationFrame(() => { pending = false; compute(); });
}

// ---- 3D scene -----------------------------------------------------------------------------

const renderer = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.setSize(window.innerWidth, window.innerHeight);
renderer.setClearColor(0xfcfcfb);
$('view').appendChild(renderer.domElement);

const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(42, window.innerWidth / window.innerHeight, 0.05, 200);
camera.up.set(0, 0, 1);
const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.maxPolarAngle = Math.PI * 0.49;

/// Frames the room: the default view of the 6 x 4 m office, scaled for other sizes.
function frameRoom() {
  const s = Math.max(state.room.width / 6, state.room.depth / 4, state.room.height / 3);
  const target = new THREE.Vector3(state.room.width * 0.4, state.room.depth * 0.5, 0.7);
  controls.target.copy(target);
  camera.position.set(target.x - 2.5 * s, target.y - 7.2 * s, target.z + 7.5 * s);
  controls.update();
}

scene.add(new THREE.HemisphereLight(0xffffff, 0xd8d6d0, 2.2));
const sun = new THREE.DirectionalLight(0xffffff, 1.6);
sun.position.set(-3, -5, 9);
scene.add(sun);

// Room: floor, two far walls (translucent) and the outline; rebuilt when the size changes.
const roomGroup = new THREE.Group();
scene.add(roomGroup);
const wallMat = new THREE.MeshStandardMaterial({ color: 0xf4f3ef, transparent: true, opacity: 0.45,
  side: THREE.DoubleSide, depthWrite: false });
const floorMat = new THREE.MeshStandardMaterial({ color: 0xf0efec });
const outlineMat = new THREE.LineBasicMaterial({ color: 0x8a8984 });

function disposeChildren(group) {
  for (const child of [...group.children]) {
    group.remove(child);
    child.geometry?.dispose();
  }
}

function buildRoom() {
  disposeChildren(roomGroup);
  const { width, depth, height } = state.room;
  const floor = new THREE.Mesh(new THREE.PlaneGeometry(width, depth), floorMat);
  floor.position.set(width / 2, depth / 2, 0);
  const backWall = new THREE.Mesh(new THREE.PlaneGeometry(width, height), wallMat);
  backWall.rotation.x = Math.PI / 2;
  backWall.position.set(width / 2, depth, height / 2);
  const sideWall = new THREE.Mesh(new THREE.PlaneGeometry(depth, height), wallMat);
  sideWall.rotation.set(Math.PI / 2, Math.PI / 2, 0);
  sideWall.position.set(width, depth / 2, height / 2);
  const outline = new THREE.LineSegments(new THREE.EdgesGeometry(new THREE.BoxGeometry(width, depth, height)), outlineMat);
  outline.position.set(width / 2, depth / 2, height / 2);
  roomGroup.add(floor, backWall, sideWall, outline);
}

// Obstacles: one mesh (+ edges) each; the mesh carries its index for picking.
const obstacleGroup = new THREE.Group();
scene.add(obstacleGroup);
const obstacleMat = new THREE.MeshStandardMaterial({ color: 0xb4b2ab, roughness: 0.8 });
const obstacleSelectedMat = new THREE.MeshStandardMaterial({ color: 0xd9c9a3, roughness: 0.8 });
const edgeMat = new THREE.LineBasicMaterial({ color: 0x52514e });
const edgeSelectedMat = new THREE.LineBasicMaterial({ color: 0xeb6834 });
let obstacleMeshes = [];

function buildObstacles() {
  disposeChildren(obstacleGroup);
  obstacleMeshes = state.obstacles.map((o, index) => {
    const selected = state.sel?.kind === 'obstacle' && state.sel.index === index;
    let mesh;
    if (o.kind === 'box') {
      const geo = new THREE.BoxGeometry(o.x1 - o.x0, o.y1 - o.y0, o.h);
      mesh = new THREE.Mesh(geo, selected ? obstacleSelectedMat : obstacleMat);
      const c = centerOf(o);
      mesh.position.set(c.x, c.y, o.h / 2);
      const edges = new THREE.LineSegments(new THREE.EdgesGeometry(geo), selected ? edgeSelectedMat : edgeMat);
      edges.position.copy(mesh.position);
      obstacleGroup.add(edges);
    } else {
      mesh = new THREE.Mesh(new THREE.SphereGeometry(o.r, 40, 20), selected ? obstacleSelectedMat : obstacleMat);
      mesh.position.set(o.x, o.y, o.z);
    }
    mesh.userData.index = index;
    obstacleGroup.add(mesh);
    return mesh;
  });
}

// Heatmap on the work plane: a canvas texture with one pixel per grid cell.
let heat = null; // { mesh, canvas, ctx, texture, nx, ny, w, d }

function ensureHeatmap(g) {
  const w = g.x1 - g.x0, d = g.y1 - g.y0;
  if (heat && heat.nx === g.nx && heat.ny === g.ny && heat.w === w && heat.d === d) return;
  if (heat) {
    scene.remove(heat.mesh);
    heat.mesh.geometry.dispose();
    heat.mesh.material.dispose();
    heat.texture.dispose();
  }
  const canvas = document.createElement('canvas');
  canvas.width = g.nx;
  canvas.height = g.ny;
  const texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.SRGBColorSpace;
  texture.magFilter = THREE.NearestFilter; // show the real cells, no blurring between them
  const mesh = new THREE.Mesh(new THREE.PlaneGeometry(w, d),
    new THREE.MeshBasicMaterial({ map: texture, side: THREE.DoubleSide }));
  scene.add(mesh);
  heat = { mesh, canvas, ctx: canvas.getContext('2d'), texture, nx: g.nx, ny: g.ny, w, d };
}

function paintHeatmap() {
  const g = last.grid;
  ensureHeatmap(g);
  heat.mesh.position.set((g.x0 + g.x1) / 2, (g.y0 + g.y1) / 2, state.workplane + 0.002);
  const img = heat.ctx.createImageData(g.nx, g.ny);
  for (let j = 0; j < g.ny; ++j) {
    const row = g.ny - 1 - j; // canvas row 0 is the top = largest y
    for (let i = 0; i < g.nx; ++i) {
      const e = last.lux[j * g.nx + i];
      const rgb = Number.isNaN(e) ? NOT_MEASURED : rampColor(e / state.scaleMax);
      const p = 4 * (row * g.nx + i);
      img.data[p] = rgb[0];
      img.data[p + 1] = rgb[1];
      img.data[p + 2] = rgb[2];
      img.data[p + 3] = 255;
    }
  }
  heat.ctx.putImageData(img, 0, 0);
  heat.texture.needsUpdate = true;
  updateLegend();
}

// Lamps: a bulb, an axis bar showing the C0 direction (asymmetric luminaires), a drop line to
// the work plane, and a larger invisible grab handle.
const lampObjects = [];
const bulbMat = new THREE.MeshBasicMaterial({ color: 0xf2b705 });
const bulbSelectedMat = new THREE.MeshBasicMaterial({ color: 0xeb6834 });
const axisMat = new THREE.MeshStandardMaterial({ color: 0x52514e });
const dropMat = new THREE.LineDashedMaterial({ color: 0x8a8984, dashSize: 0.06, gapSize: 0.05 });
const handleMat = new THREE.MeshBasicMaterial({ transparent: true, opacity: 0, depthWrite: false });

const dropPoints = (l) => [new THREE.Vector3(l.x, l.y, l.z), new THREE.Vector3(l.x, l.y, state.workplane)];
const isAsymmetric = (l) => l.lum !== ISOTROPIC && catalog.get(l.lum)?.info?.symmetry !== 1;

function rebuildLamps() {
  for (const o of lampObjects) {
    scene.remove(o.group, o.drop);
    o.drop.geometry.dispose();
  }
  lampObjects.length = 0;
  state.lamps.forEach((l, index) => {
    const group = new THREE.Group();
    const bulb = new THREE.Mesh(new THREE.SphereGeometry(0.07, 24, 12), bulbMat);
    const axis = new THREE.Mesh(new THREE.BoxGeometry(0.7, 0.06, 0.03), axisMat);
    axis.position.z = 0.06;
    const handle = new THREE.Mesh(new THREE.SphereGeometry(0.22, 12, 8), handleMat);
    handle.userData.index = index;
    const drop = new THREE.Line(new THREE.BufferGeometry().setFromPoints(dropPoints(l)), dropMat);
    drop.computeLineDistances();
    group.add(bulb, axis, handle);
    scene.add(group, drop);
    lampObjects.push({ group, bulb, axis, handle, drop, data: l });
  });
  updateLampMeshes();
}

function updateLampMeshes() {
  lampObjects.forEach((o, index) => {
    const selected = state.sel?.kind === 'lamp' && state.sel.index === index;
    o.group.position.set(o.data.x, o.data.y, o.data.z);
    o.bulb.material = selected ? bulbSelectedMat : bulbMat;
    o.bulb.scale.setScalar(selected ? 1.35 : 1);
    o.axis.visible = isAsymmetric(o.data);
    o.axis.rotation.z = THREE.MathUtils.degToRad(o.data.rotation);
    o.drop.geometry.setFromPoints(dropPoints(o.data));
    o.drop.computeLineDistances();
  });
  syncControls();
}

/// Everything that depends on the room, the obstacles or the selection.
function rebuildAll() {
  fitToRoom();
  buildRoom();
  buildObstacles();
  rebuildLamps();
  scheduleCompute();
}

// ---- Interaction: select and drag lamps and obstacles -------------------------------------

const raycaster = new THREE.Raycaster();
const pointer = new THREE.Vector2();
let drag = null; // { kind, index, plane, offset }

function aim(event) {
  const rect = renderer.domElement.getBoundingClientRect();
  pointer.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
  pointer.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;
  raycaster.setFromCamera(pointer, camera);
}

const handles = () => lampObjects.map((o) => o.handle);
const horizontalPlane = (z) => new THREE.Plane(new THREE.Vector3(0, 0, 1), -z);

// Capture phase: runs before OrbitControls, so grabbing an object does not rotate the view.
renderer.domElement.addEventListener('pointerdown', (e) => {
  if (e.button !== 0) return;
  aim(e);
  const lampHit = raycaster.intersectObjects(handles(), false)[0];
  const obstacleHit = lampHit ? null : raycaster.intersectObjects(obstacleMeshes, false)[0];
  const hit = lampHit ?? obstacleHit;
  if (!hit) return;
  e.stopImmediatePropagation();
  const kind = lampHit ? 'lamp' : 'obstacle';
  const index = hit.object.userData.index;
  const c = kind === 'lamp' ? state.lamps[index] : centerOf(state.obstacles[index]);
  // Drag in a horizontal plane through the grab point, keeping the grab offset.
  const plane = horizontalPlane(hit.point.z);
  drag = { kind, index, plane, offset: new THREE.Vector2(c.x - hit.point.x, c.y - hit.point.y) };
  state.sel = { kind, index };
  if (kind === 'obstacle') buildObstacles();
  updateLampMeshes();
  renderer.domElement.setPointerCapture(e.pointerId);
  renderer.domElement.style.cursor = 'grabbing';
}, { capture: true });

renderer.domElement.addEventListener('pointermove', (e) => {
  aim(e);
  const tip = $('tip');
  if (drag) {
    const p = new THREE.Vector3();
    if (raycaster.ray.intersectPlane(drag.plane, p)) {
      const { width, depth } = state.room;
      if (drag.kind === 'lamp') {
        const lamp = state.lamps[drag.index];
        lamp.x = clamp(p.x + drag.offset.x, 0.05, width - 0.05);
        lamp.y = clamp(p.y + drag.offset.y, 0.05, depth - 0.05);
        updateLampMeshes();
      } else {
        const o = state.obstacles[drag.index];
        const half = halfSize(o), c = centerOf(o);
        const tx = clamp(p.x + drag.offset.x, half.x, width - half.x);
        const ty = clamp(p.y + drag.offset.y, half.y, depth - half.y);
        shift(o, tx - c.x, ty - c.y);
        buildObstacles();
      }
      scheduleCompute();
    }
    tip.style.display = 'none';
    return;
  }
  const overObject = raycaster.intersectObjects([...handles(), ...obstacleMeshes], false).length > 0;
  renderer.domElement.style.cursor = overObject ? 'grab' : '';
  const hit = heat && !overObject ? raycaster.intersectObject(heat.mesh, false)[0] : null;
  if (hit && last) {
    const g = last.grid;
    const i = Math.min(g.nx - 1, Math.floor(hit.uv.x * g.nx));
    const j = Math.min(g.ny - 1, Math.floor(hit.uv.y * g.ny));
    const lux = last.lux[j * g.nx + i];
    tip.textContent = `x ${hit.point.x.toFixed(2)} m, y ${hit.point.y.toFixed(2)} m: ` +
      (Number.isNaN(lux) ? 'inside obstacle' : `${lux.toFixed(0)} lx`);
    tip.style.left = `${e.clientX + 14}px`;
    tip.style.top = `${e.clientY + 14}px`;
    tip.style.display = 'block';
  } else {
    tip.style.display = 'none';
  }
});

function endDrag(e) {
  if (!drag) return;
  drag = null;
  if (renderer.domElement.hasPointerCapture(e.pointerId)) {
    renderer.domElement.releasePointerCapture(e.pointerId);
  }
  renderer.domElement.style.cursor = '';
}
renderer.domElement.addEventListener('pointerup', endDrag);
renderer.domElement.addEventListener('pointercancel', endDrag);
renderer.domElement.addEventListener('pointerleave', () => { $('tip').style.display = 'none'; });

// ---- Polar diagram (C0-C180 and C90-C270 planes, nadir at the bottom) ---------------------

const curve = (id, c) => Array.from(engine.polarCurve(id, c, 72)); // γ = 0..180° in 2.5° steps

function drawPolar() {
  const canvas = $('polar');
  const ctx = canvas.getContext('2d');
  const w = canvas.width, h = canvas.height;
  ctx.clearRect(0, 0, w, h);
  const lamp = selectedLamp();
  if (!lamp) {
    $('polar-info').textContent = '';
    return;
  }
  const cx = w / 2, cy = h / 2, radius = Math.min(w, h) / 2 - 8;
  const scale = lamp.lum === ISOTROPIC ? 1 : lamp.dimming;
  const planes = lamp.lum === ISOTROPIC
    ? null
    : { c0: curve(lamp.lum, 0), c180: curve(lamp.lum, 180), c90: curve(lamp.lum, 90), c270: curve(lamp.lum, 270) };
  const peak = lamp.lum === ISOTROPIC
    ? lamp.cd
    : Math.max(...planes.c0, ...planes.c180, ...planes.c90, ...planes.c270) * scale;

  ctx.strokeStyle = '#e4e3df';
  ctx.lineWidth = 1;
  for (const f of [0.25, 0.5, 0.75, 1]) {
    ctx.beginPath();
    ctx.arc(cx, cy, radius * f, 0, 2 * Math.PI);
    ctx.stroke();
  }
  ctx.beginPath();
  ctx.moveTo(cx - radius, cy); ctx.lineTo(cx + radius, cy);
  ctx.moveTo(cx, cy - radius); ctx.lineTo(cx, cy + radius);
  ctx.stroke();
  ctx.fillStyle = '#8a8984';
  ctx.font = '10px system-ui, sans-serif';
  ctx.fillText('0° (down)', cx + 4, h - 2);
  ctx.fillText('90°', w - 22, cy - 4);

  // γ measured from straight down; right half = C0 (or C90), left half = C180 (or C270).
  const trace = (right, left, color, dashed) => {
    ctx.beginPath();
    ctx.strokeStyle = color;
    ctx.lineWidth = 2;
    ctx.setLineDash(dashed ? [5, 4] : []);
    const pts = [];
    right.forEach((v, k) => pts.push([+1, k * 2.5, v]));
    for (let k = left.length - 1; k >= 0; --k) pts.push([-1, k * 2.5, left[k]]);
    pts.forEach(([side, g, v], n) => {
      const r = peak > 0 ? (v * scale / peak) * radius : 0;
      const a = (g * Math.PI) / 180;
      const x = cx + side * r * Math.sin(a);
      const y = cy + r * Math.cos(a);
      if (n === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    });
    ctx.closePath();
    ctx.stroke();
    ctx.setLineDash([]);
  };
  if (planes) {
    trace(planes.c90, planes.c270, '#eb6834', true);
    trace(planes.c0, planes.c180, '#2a78d6', false);
  } else {
    ctx.beginPath();
    ctx.strokeStyle = '#2a78d6';
    ctx.lineWidth = 2;
    ctx.arc(cx, cy, radius, 0, 2 * Math.PI);
    ctx.stroke();
  }

  const info = catalog.get(lamp.lum)?.info;
  $('polar-info').textContent = `peak ${fmt(peak)} cd` +
    (info ? ` · ${fmt(info.fluxLm * scale)} lm` : ` · ${fmt(4 * Math.PI * lamp.cd)} lm`);
}

// ---- Panel --------------------------------------------------------------------------------

function showStats() {
  $('s-avg').innerHTML = `${fmt(last.avgLux)} <small>lx</small>`;
  $('s-min').innerHTML = `${fmt(last.minLux)} <small>lx</small>`;
  $('s-max').innerHTML = `${fmt(last.maxLux)} <small>lx</small>`;
  $('s-u0').textContent = fmt(last.uniformityU0, 2);
  const total = last.grid.nx * last.grid.ny;
  $('s-meta').textContent = `${last.grid.nx} × ${last.grid.ny} = ${fmt(total)} points ` +
    `(${fmt(total - last.validPoints)} inside obstacles) · ${state.lamps.length} lamps · ` +
    `${state.obstacles.length} obstacles · computed in ${fmt(last.elapsedMs, 1)} ms (WebAssembly, 1 thread)`;
}

function updateLegend() {
  $('legend-bar').style.background = `linear-gradient(to right, ${RAMP.join(', ')})`;
  const ticks = [0, 0.25, 0.5, 0.75, 1].map((t) => `<span>${fmt(t * state.scaleMax)}</span>`);
  ticks[ticks.length - 1] = `<span>${fmt(state.scaleMax)} lx</span>`;
  $('legend-ticks').innerHTML = ticks.join('');
}

function fillCatalog() {
  const select = $('lum');
  select.innerHTML = '';
  for (const [id, entry] of catalog) {
    const option = document.createElement('option');
    option.value = String(id);
    option.textContent = entry.label;
    select.appendChild(option);
  }
}

function setRange(id, value, text) {
  $(id).value = value;
  $(`${id}-out`).textContent = text;
}

function syncControls() {
  const lamp = selectedLamp();
  const obstacle = selectedObstacle();

  $('lamp-section').hidden = !!obstacle;
  $('obstacle-section').hidden = !obstacle;
  for (const id of ['lum', 'cd', 'dim', 'rot', 'apply-all']) $(id).disabled = !lamp;
  $('remove').disabled = !lamp && !obstacle;
  $('sel-label').textContent = lamp ? `#${state.sel.index + 1} of ${state.lamps.length}` : '(none: click a lamp)';
  if (lamp) {
    $('lum').value = String(lamp.lum);
    setRange('cd', lamp.cd, `${fmt(lamp.cd)} cd`);
    setRange('dim', Math.round(lamp.dimming * 100), `${Math.round(lamp.dimming * 100)} %`);
    setRange('rot', lamp.rotation, `${lamp.rotation}°`);
  }
  const photometric = lamp && lamp.lum !== ISOTROPIC;
  $('row-cd').hidden = !!photometric;
  $('row-dim').hidden = !photometric;
  $('row-rot').hidden = !(lamp && isAsymmetric(lamp));

  if (obstacle) {
    const isBox = obstacle.kind === 'box';
    $('obs-label').textContent = `${isBox ? 'box' : 'sphere'} #${state.sel.index + 1}`;
    $('box-rows').hidden = !isBox;
    $('sphere-rows').hidden = isBox;
    $('bh').max = state.room.height;
    $('sz').max = state.room.height;
    if (isBox) {
      const w = obstacle.x1 - obstacle.x0, d = obstacle.y1 - obstacle.y0;
      setRange('bw', w, `${w.toFixed(2)} m`);
      setRange('bd', d, `${d.toFixed(2)} m`);
      setRange('bh', obstacle.h, `${obstacle.h.toFixed(2)} m`);
    } else {
      setRange('sr', obstacle.r, `${obstacle.r.toFixed(2)} m`);
      setRange('sz', obstacle.z, `${obstacle.z.toFixed(2)} m`);
    }
  }

  $('wp').max = maxWorkplane();
  setRange('wp', state.workplane, `${state.workplane.toFixed(2)} m`);
  $('cell').value = String(state.cell);
  $('shadows').checked = state.shadows;
  setRange('rw', state.room.width, `${state.room.width.toFixed(1)} m`);
  setRange('rd', state.room.depth, `${state.room.depth.toFixed(1)} m`);
  setRange('rh', state.room.height, `${state.room.height.toFixed(1)} m`);
  drawPolar();
}

// Lamp controls.
const onLamp = (id, apply) => $(id).addEventListener('input', (e) => {
  const lamp = selectedLamp();
  if (!lamp) return;
  apply(lamp, Number(e.target.value));
  updateLampMeshes();
  scheduleCompute();
});
$('lum').addEventListener('change', (e) => {
  const lamp = selectedLamp();
  if (!lamp) return;
  lamp.lum = Number(e.target.value);
  updateLampMeshes();
  scheduleCompute();
});
onLamp('cd', (l, v) => { l.cd = v; });
onLamp('dim', (l, v) => { l.dimming = v / 100; });
onLamp('rot', (l, v) => { l.rotation = v; });
$('apply-all').addEventListener('click', () => {
  const lamp = selectedLamp();
  if (!lamp) return;
  for (const l of state.lamps) Object.assign(l, { lum: lamp.lum, cd: lamp.cd, dimming: lamp.dimming, rotation: lamp.rotation });
  updateLampMeshes();
  scheduleCompute();
});
$('ldt-file').addEventListener('change', async (e) => {
  const file = e.target.files[0];
  e.target.value = ''; // allow loading the same file again
  if (!file) return;
  try {
    const id = addPhotometry(await file.text(), undefined);
    const entry = catalog.get(id);
    entry.label = `${entry.info.name || file.name} (${file.name})`;
    fillCatalog();
    if (!selectedLamp()) state.sel = { kind: 'lamp', index: 0 };
    if (selectedLamp()) selectedLamp().lum = id;
    $('error').textContent = '';
    updateLampMeshes();
    scheduleCompute();
  } catch (err) {
    $('error').textContent = `Could not read ${file.name}: ${err.message}`;
  }
});

// Obstacle controls.
const onObstacle = (id, apply) => $(id).addEventListener('input', (e) => {
  const o = selectedObstacle();
  if (!o) return;
  apply(o, Number(e.target.value));
  fitToRoom();
  buildObstacles();
  syncControls();
  scheduleCompute();
});
onObstacle('bw', (o, v) => { resizeBox(o, v, o.y1 - o.y0); });
onObstacle('bd', (o, v) => { resizeBox(o, o.x1 - o.x0, v); });
onObstacle('bh', (o, v) => { o.h = v; });
onObstacle('sr', (o, v) => { o.r = v; });
onObstacle('sz', (o, v) => { o.z = v; });

// Room and global controls.
const onRoom = (id, key) => $(id).addEventListener('input', (e) => {
  state.room[key] = Number(e.target.value);
  rebuildAll();
});
onRoom('rw', 'width');
onRoom('rd', 'depth');
onRoom('rh', 'height');
$('wp').addEventListener('input', (e) => {
  state.workplane = Number(e.target.value);
  updateLampMeshes();
  scheduleCompute();
});
$('cell').addEventListener('change', (e) => { state.cell = Number(e.target.value); scheduleCompute(); });
$('shadows').addEventListener('change', (e) => { state.shadows = e.target.checked; scheduleCompute(); });

$('add').addEventListener('click', () => {
  const template = selectedLamp() ?? newLamp(0, 0);
  state.lamps.push({ ...template, x: state.room.width / 2, y: state.room.depth / 2 });
  state.sel = { kind: 'lamp', index: state.lamps.length - 1 };
  rebuildAll();
});
$('add-box').addEventListener('click', () => {
  const cx = state.room.width / 2, cy = state.room.depth / 2; // a 1.2 x 0.6 m desk, 0.72 m high
  state.obstacles.push({ kind: 'box', x0: cx - 0.6, x1: cx + 0.6, y0: cy - 0.3, y1: cy + 0.3, h: 0.72 });
  state.sel = { kind: 'obstacle', index: state.obstacles.length - 1 };
  rebuildAll();
});
$('remove').addEventListener('click', () => {
  if (selectedLamp()) state.lamps.splice(state.sel.index, 1);
  else if (selectedObstacle()) state.obstacles.splice(state.sel.index, 1);
  state.sel = null;
  rebuildAll();
});
$('reset').addEventListener('click', () => {
  Object.assign(state, {
    room: defaultRoom(), lamps: defaultLamps(), obstacles: defaultObstacles(),
    sel: { kind: 'lamp', index: 0 }, workplane: 0.75, cell: 0.05, shadows: true, scaleMax: 600,
  });
  history.replaceState(null, '', window.location.pathname);
  notice = '';
  rebuildAll();
  frameRoom();
});

// ---- Export and share ---------------------------------------------------------------------

function download(filename, blob) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

/// Same format as lumacore_cli, so tools/heatmap.py and tools/compare_grids.py read it.
/// JavaScript's String(number) is the shortest text that parses back to the same double.
function toCsv() {
  const g = last.grid;
  const { width, depth, height } = state.room;
  const lines = [
    `# lumacore,${lc.version()}`, '# scenario,web export', `# shadows,${state.shadows ? 1 : 0}`,
    `# room,${width},${depth},${height}`, `# grid,${g.nx},${g.ny},${g.x0},${g.y0},${g.x1},${g.y1},${state.workplane}`,
  ];
  for (const l of state.lamps) {
    const peak = l.lum === ISOTROPIC ? l.cd : (catalog.get(l.lum)?.info?.maxCd ?? 0) * l.dimming;
    lines.push(`# light,${l.x},${l.y},${l.z},${peak}`);
  }
  for (const o of state.obstacles) {
    if (o.kind === 'box') lines.push(`# box,${o.x0},${o.y0},0,${o.x1},${o.y1},${o.h}`);
    else lines.push(`# sphere,${o.x},${o.y},${o.z},${o.r}`);
  }
  lines.push(`# stats,${last.minLux},${last.maxLux},${last.avgLux},${last.uniformityU0},${last.validPoints}`);
  lines.push('i,j,x_m,y_m,z_m,lux');
  const dx = (g.x1 - g.x0) / g.nx, dy = (g.y1 - g.y0) / g.ny;
  for (let j = 0; j < g.ny; ++j) {
    for (let i = 0; i < g.nx; ++i) {
      const e = last.lux[j * g.nx + i];
      lines.push(`${i},${j},${g.x0 + dx * (i + 0.5)},${g.y0 + dy * (j + 0.5)},${state.workplane},${Number.isNaN(e) ? 'nan' : String(e)}`);
    }
  }
  return `${lines.join('\n')}\n`;
}

$('dl-csv').addEventListener('click', () => {
  if (last) download('lumacore-grid.csv', new Blob([toCsv()], { type: 'text/csv' }));
});
$('dl-png').addEventListener('click', () => {
  renderer.render(scene, camera);
  renderer.domElement.toBlob((blob) => blob && download('lumacore-3d.png', blob), 'image/png');
});

// Share links: the design is JSON in the URL fragment (#s=...), base64url-encoded.
const r3 = (v) => Math.round(v * 1000) / 1000;
const toBase64Url = (text) => btoa(String.fromCharCode(...new TextEncoder().encode(text)))
  .replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
const fromBase64Url = (s) => new TextDecoder().decode(
  Uint8Array.from(atob(s.replace(/-/g, '+').replace(/_/g, '/')), (c) => c.charCodeAt(0)));

function encodeDesign() {
  let uploadedDropped = false;
  const lamps = state.lamps.map((l) => {
    const key = catalog.get(l.lum)?.key;
    if (!key) uploadedDropped = true; // uploaded files are too large for a link
    return [r3(l.x), r3(l.y), key ?? 'iso', l.cd, r3(l.dimming), l.rotation];
  });
  const obstacles = state.obstacles.map((o) => (o.kind === 'box'
    ? ['b', r3(o.x0), r3(o.y0), r3(o.x1), r3(o.y1), r3(o.h)]
    : ['s', r3(o.x), r3(o.y), r3(o.z), r3(o.r)]));
  const design = { v: 1, room: [state.room.width, state.room.depth, state.room.height],
    wp: state.workplane, cell: state.cell, sh: state.shadows ? 1 : 0, lamps, obstacles };
  return { hash: `#s=${toBase64Url(JSON.stringify(design))}`, uploadedDropped };
}

/// Applies a design from a share link. Every value is range-checked; nothing is executed.
function decodeDesign(hash) {
  let design;
  try {
    design = JSON.parse(fromBase64Url(hash.slice(3)));
  } catch {
    throw new Error('the link is damaged or incomplete');
  }
  if (!design || design.v !== 1 || !Array.isArray(design.room)) throw new Error('unknown link format');
  const [w, d, h] = design.room;
  state.room = { width: clamp(num(w, 6), 2, 12), depth: clamp(num(d, 4), 2, 12), height: clamp(num(h, 3), 2.2, 5) };
  state.workplane = num(design.wp, 0.75);
  state.cell = [0.1, 0.05, 0.025].includes(design.cell) ? design.cell : 0.05;
  state.shadows = design.sh !== 0;
  state.lamps = (design.lamps ?? []).slice(0, 100).map(([x, y, key, cd, dimming, rotation]) => ({
    ...newLamp(num(x, 1), num(y, 1)),
    lum: idByKey[key] ?? ISOTROPIC,
    cd: clamp(num(cd, 800), 0, 100000),
    dimming: clamp(num(dimming, 1), 0, 1.5),
    rotation: clamp(num(rotation, 0), 0, 180),
  }));
  state.obstacles = (design.obstacles ?? []).slice(0, 100).map((o) => {
    if (o[0] === 'b') {
      const x0 = num(o[1], 1), y0 = num(o[2], 1);
      // Corners must be ordered and the box at least 5 cm wide.
      return { kind: 'box', x0, y0, x1: Math.max(num(o[3], x0 + 0.5), x0 + 0.05),
        y1: Math.max(num(o[4], y0 + 0.5), y0 + 0.05), h: clamp(num(o[5], 1), 0.05, 5) };
    }
    return { kind: 'sphere', x: num(o[1], 1), y: num(o[2], 1), z: num(o[3], 1), r: clamp(num(o[4], 0.25), 0.05, 2) };
  });
  state.sel = state.lamps.length ? { kind: 'lamp', index: 0 } : null;
}

$('share').addEventListener('click', async () => {
  const { hash, uploadedDropped } = encodeDesign();
  const url = `${window.location.origin}${window.location.pathname}${hash}`;
  history.replaceState(null, '', hash);
  let copied = false;
  try {
    await navigator.clipboard.writeText(url);
    copied = true;
  } catch { /* clipboard blocked: show the link instead */ }
  $('share-url').hidden = copied;
  $('share-url').value = url;
  $('share-note').textContent = (copied ? 'Link copied to the clipboard.' : 'Copy this link:') +
    (uploadedDropped ? ' Uploaded .ldt files are not included; those lamps become isotropic.' : '');
});

// ---- Start --------------------------------------------------------------------------------

window.addEventListener('resize', () => {
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight);
});

// Initial design: a share link (#s=...) or ?luminaire=linear_batwing&rotation=90&dimming=80.
const params = new URLSearchParams(window.location.search);
try {
  if (window.location.hash.startsWith('#s=')) decodeDesign(window.location.hash);
} catch (err) {
  notice = `Could not read the share link (${err.message}); showing the default office.`;
  Object.assign(state, { room: defaultRoom(), lamps: defaultLamps(), obstacles: defaultObstacles(),
    sel: { kind: 'lamp', index: 0 } });
}
if (params.get('luminaire') in idByKey) {
  const lum = idByKey[params.get('luminaire')];
  const rotation = clamp(num(params.get('rotation'), 0), 0, 180);
  const dimming = clamp(num(params.get('dimming'), 100), 0, 150) / 100;
  for (const l of state.lamps) Object.assign(l, { lum, rotation, dimming });
}

$('version').textContent = `engine ${lc.version()}`;
$('loading').remove();
$('panel').hidden = false;
fillCatalog();
frameRoom();
rebuildAll();
compute();

renderer.setAnimationLoop(() => {
  controls.update();
  renderer.render(scene, camera);
});

// LumaCore Web: interactive 3D front end for the C++ engine compiled to WebAssembly.
// Coordinates are the engine's: metres, right-handed, +Z up.

import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';
import createLumaCore from './lumacore.js';
import { CATALOG, itemGeometry, placePoints, placedSolid } from './furniture.js';
import { tessellate, flatNormals, evaluationPoints, uniqueVertices } from './surfaces.js';
import {
  ISOTROPIC, newLamp, referenceScene, furnishedScene, isSolidItem, furnitureForEngine, footprint, TASK_TARGET, taskStats,
} from './scene.js';

// ---- Constants ----------------------------------------------------------------------------

const BORDER = 0.5;          // excluded band along the walls (EN 12464-1 border zone)
const LAMP_BELOW_CEILING = 0.2;
const SAMPLE_LDT = ['downlight_wide', 'spot_narrow', 'linear_batwing'];
const MAX_MODEL_TRIANGLES = 300000;

// Work-plane colours. Lux: sequential single-hue ramp (light = low, dark = high), the same as
// tools/heatmap.py. Light appearance: dark to warm white, as the lit surface would look.
const RAMP = ['#cde2fb', '#b7d3f6', '#9ec5f4', '#86b6ef', '#6da7ec', '#5598e7', '#3987e5',
  '#2a78d6', '#256abf', '#1c5cab', '#184f95', '#104281', '#0d366b'];
const APPEARANCE = ['#17150f', '#6b5f4c', '#c9b99a', '#fff6e6'];
const NOT_MEASURED = [0xc9, 0xc8, 0xc2];

// ---- Helpers ------------------------------------------------------------------------------

const $ = (id) => document.getElementById(id);
const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
/// A finite number from untrusted input (link, JSON), else the fallback. Missing values must not
/// become 0: Number(null) and Number('') are 0 in JavaScript.
const num = (v, fallback) =>
  (v === null || v === undefined || v === '' || !Number.isFinite(Number(v)) ? fallback : Number(v));
const fmt = (v, d = 0) => v.toLocaleString('en-US', { minimumFractionDigits: d, maximumFractionDigits: d });
const toRgb = (hexes) => hexes.map((h) => [1, 3, 5].map((k) => parseInt(h.slice(k, k + 2), 16)));
const RAMP_RGB = toRgb(RAMP);
const APPEARANCE_RGB = toRgb(APPEARANCE);

function rampColor(stops, t) {
  const x = clamp(t, 0, 1) * (stops.length - 1);
  const k = Math.min(stops.length - 2, Math.floor(x));
  const f = x - k;
  const a = stops[k], b = stops[k + 1];
  return [0, 1, 2].map((c) => Math.round(a[c] + (b[c] - a[c]) * f));
}

// ---- Engine and luminaire catalogue -------------------------------------------------------

let lc, engine;
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
const lumOr = (key) => idByKey[key] ?? ISOTROPIC;

// ---- Scenes (definitions in scene.js, shared with tools/web_check.mjs) --------------------

const SCENES = {
  furnished: () => furnishedScene({ linear: lumOr('linear_batwing'), down: lumOr('downlight_wide') }),
  reference: referenceScene,
};

const state = {
  ...SCENES.furnished(),
  sel: { kind: 'lamp', index: 0 }, // { kind: 'lamp' | 'obstacle' | 'furniture', index } or null
  workplane: 0.75,
  cell: 0.05,
  shadows: true,
  view: 'room-appearance', // 'room-appearance' | 'room-lux' | 'appearance' | 'lux'
  scaleMax: 600, // colour scale top [lx]; grows if exceeded, never jumps while dragging
};
const VIEWS = ['room-appearance', 'room-lux', 'appearance', 'lux'];
const isRoomView = () => state.view.startsWith('room');
const isAppearance = () => state.view.endsWith('appearance');

function loadScene(name) {
  Object.assign(state, (SCENES[name] ?? furnishedScene)(), {
    sel: { kind: 'lamp', index: 0 }, workplane: 0.75, cell: 0.05, shadows: true, scaleMax: 600,
  });
  $('preset').value = name in SCENES ? name : 'furnished';
}

// Imported models: id -> { name, object (normalized THREE.Object3D), local (Float64Array), bounds }.
const models = new Map();
let nextModelId = 1;

// ---- Geometry helpers ---------------------------------------------------------------------

const centerOf = (o) => (o.kind === 'box' ? { x: (o.x0 + o.x1) / 2, y: (o.y0 + o.y1) / 2 } : { x: o.x, y: o.y });
const halfSize = (o) => (o.kind === 'box' ? { x: (o.x1 - o.x0) / 2, y: (o.y1 - o.y0) / 2 } : { x: o.r, y: o.r });
function shift(o, dx, dy) {
  if (o.kind === 'box') { o.x0 += dx; o.x1 += dx; o.y0 += dy; o.y1 += dy; } else { o.x += dx; o.y += dy; }
}
function resizeBox(o, w, d) {
  const c = centerOf(o);
  Object.assign(o, { x0: c.x - w / 2, x1: c.x + w / 2, y0: c.y - d / 2, y1: c.y + d / 2 });
}

const itemDef = (item) => (item.type === 'model' ? models.get(item.modelId) : itemGeometry(item.type));

function clampItem(item) {
  const f = footprint(itemDef(item).bounds, item.rotation), { width, depth } = state.room;
  item.x = f.maxX - f.minX >= width ? width / 2 - (f.minX + f.maxX) / 2 : clamp(item.x, -f.minX, width - f.maxX);
  item.y = f.maxY - f.minY >= depth ? depth / 2 - (f.minY + f.maxY) / 2 : clamp(item.y, -f.minY, depth - f.maxY);
}

const maxWorkplane = () => Math.min(1.5, state.room.height - LAMP_BELOW_CEILING - 0.3);

/// Keeps lamps, obstacles and furniture inside the room after it was resized or loaded.
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
  for (const item of state.furniture) clampItem(item);
  state.workplane = clamp(state.workplane, 0, maxWorkplane());
}

// ---- Simulation ---------------------------------------------------------------------------

let last = null;   // latest successful run: { ..., lux: Float64Array, grid }
let surfaceGeneration = 0; // bumped by every new run; a running whole-room evaluation checks it
let notice = '';   // persistent message (e.g. an unreadable share link), shown after each run
let geometryDirty = true;  // engine geometry must be re-sent
let litDirty = true;       // whole-room surface meshes must be rebuilt
const markGeometry = () => { geometryDirty = true; litDirty = true; };

function gridSpec() {
  const { width, depth } = state.room;
  const x0 = BORDER, y0 = BORDER, x1 = width - BORDER, y1 = depth - BORDER;
  return { x0, y0, x1, y1,
    nx: Math.max(1, Math.round((x1 - x0) / state.cell)), ny: Math.max(1, Math.round((y1 - y0) / state.cell)) };
}

/// Sends room, obstacles and furniture to the engine. The engine rebuilds its BVH once, on the
/// next simulate(); dragging a lamp does not come through here.
function syncGeometry() {
  const { width, depth, height } = state.room;
  engine.setRoom(width, depth, height);
  engine.clearObstacles();
  for (const o of state.obstacles) {
    if (o.kind === 'box') engine.addBox(o.x0, o.y0, 0, o.x1, o.y1, o.h);
    else engine.addSphere(o.x, o.y, o.z, o.r);
  }
  for (const item of state.furniture) {
    if (item.type === 'model') { // imported models: every triangle casts shadows
      engine.addTriangles(placePoints(models.get(item.modelId).local, item.x, item.y, item.rotation));
      continue;
    }
    const geometry = furnitureForEngine(item); // same rule as tools/web_check.mjs
    if (geometry.box) engine.addBox(...geometry.box);
    else engine.addTriangles(geometry.triangles);
  }
  geometryDirty = false;
}

function compute() {
  if (geometryDirty) syncGeometry();
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
  requestRender();
  document.body.dataset.avgLux = r.avgLux.toFixed(3); // for automated checks
  // Lights or geometry changed: a whole-room evaluation still running is now stale. Cancel it
  // rather than let it finish its remaining chunks with the new lights (a mixed result).
  ++surfaceGeneration;
  if (isRoomView()) scheduleSurfaces();
}

/// sRGB bytes for an illuminance, in the current view's colour scale.
function luxToRgb(e) {
  if (Number.isNaN(e)) return NOT_MEASURED;
  if (isAppearance()) return rampColor(APPEARANCE_RGB, Math.pow(1 - Math.exp(-e / (0.45 * state.scaleMax)), 0.85));
  return rampColor(RAMP_RGB, e / state.scaleMax);
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
renderer.toneMapping = THREE.ACESFilmicToneMapping;
$('view').appendChild(renderer.domElement);

const scene = new THREE.Scene();
// Soft image-based lighting for the furniture (display only; the lux values come from the engine).
scene.environment = new THREE.PMREMGenerator(renderer).fromScene(new RoomEnvironment(), 0.04).texture;
scene.add(new THREE.HemisphereLight(0xffffff, 0xd8d6d0, 0.6));
const sun = new THREE.DirectionalLight(0xffffff, 0.9);
sun.position.set(-3, -5, 9);
scene.add(sun);

const camera = new THREE.PerspectiveCamera(42, window.innerWidth / window.innerHeight, 0.05, 200);
camera.up.set(0, 0, 1);
const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.maxPolarAngle = Math.PI * 0.49;

// Render on demand: draw a frame only when something changed (camera, objects, results), not
// 60 times per second. Saves CPU, GPU and battery; damping keeps requesting frames until the
// camera settles.
let renderRequested = false;
function requestRender() {
  if (renderRequested) return;
  renderRequested = true;
  requestAnimationFrame(() => {
    renderRequested = false;
    if (controls.update()) requestRender(); // still moving (damping)
    renderer.render(scene, camera);
  });
}
controls.addEventListener('change', requestRender);

/// Frames the room: the original view of the 6 x 4 m office, scaled for other sizes.
function frameRoom() {
  const s = Math.max(state.room.width / 6, state.room.depth / 4, state.room.height / 3);
  const target = new THREE.Vector3(state.room.width * 0.4, state.room.depth * 0.5, 0.7);
  controls.target.copy(target);
  camera.position.set(target.x - 2.5 * s, target.y - 7.2 * s, target.z + 7.5 * s);
  controls.update();
}

function disposeChildren(group) {
  for (const child of [...group.children]) {
    group.remove(child);
    child.traverse?.((o) => o.geometry?.dispose());
  }
}

// Room: floor, two far walls (translucent) and the outline; rebuilt when the size changes.
const roomGroup = new THREE.Group();
scene.add(roomGroup);
const floorMat = new THREE.MeshStandardMaterial({ color: 0xd8d0c2, roughness: 0.9 });
const wallMat = new THREE.MeshStandardMaterial({ color: 0xf4f3ef, transparent: true, opacity: 0.4,
  side: THREE.DoubleSide, depthWrite: false, roughness: 0.95 });
const outlineMat = new THREE.LineBasicMaterial({ color: 0x8a8984 });

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
  for (const surface of [floor, backWall, sideWall]) surface.userData.surface = true; // replaced in whole-room views
  roomGroup.add(floor, backWall, sideWall, outline);
}

// Obstacles (boxes and spheres of the reference scene or added by the user).
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
    mesh.userData.pick = { kind: 'obstacle', index };
    obstacleGroup.add(mesh);
    return mesh;
  });
  requestRender();
}

// Furniture: one vertex-coloured mesh per item, built from the same triangles the engine gets.
const furnitureGroup = new THREE.Group();
scene.add(furnitureGroup);
const furnitureMat = new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.7, metalness: 0.05, side: THREE.DoubleSide });
const furnitureGeometries = new Map(); // type -> BufferGeometry (shared by all items of a type)
let furnitureObjects = [];

function furnitureGeometry(type) {
  if (furnitureGeometries.has(type)) return furnitureGeometries.get(type);
  const { positions, colors } = itemGeometry(type);
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.Float32BufferAttribute(Float32Array.from(positions), 3));
  const rgb = new Float32Array(colors.length * 9);
  const color = new THREE.Color();
  colors.forEach((hex, t) => {
    color.setHex(hex); // sRGB -> linear working space
    for (let v = 0; v < 3; ++v) rgb.set([color.r, color.g, color.b], t * 9 + v * 3);
  });
  geo.setAttribute('color', new THREE.Float32BufferAttribute(rgb, 3));
  geo.computeVertexNormals();
  furnitureGeometries.set(type, geo);
  return geo;
}

const selectionGroup = new THREE.Group(); // selection outline, visible in every view
scene.add(selectionGroup);

function buildFurniture() {
  for (const child of [...furnitureGroup.children]) furnitureGroup.remove(child); // geometries are shared
  disposeChildren(selectionGroup);
  furnitureObjects = state.furniture.map((item, index) => {
    const group = new THREE.Group();
    group.position.set(item.x, item.y, 0);
    group.rotation.z = THREE.MathUtils.degToRad(item.rotation);
    if (item.type === 'model') {
      group.add(models.get(item.modelId).object);
    } else {
      group.add(new THREE.Mesh(furnitureGeometry(item.type), furnitureMat));
    }
    group.traverse((o) => { o.userData.pick = { kind: 'furniture', index }; });
    furnitureGroup.add(group);
    if (state.sel?.kind === 'furniture' && state.sel.index === index) {
      group.updateMatrixWorld(true);
      selectionGroup.add(new THREE.BoxHelper(group, 0xeb6834));
    }
    return group;
  });
  requestRender();
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
    new THREE.MeshBasicMaterial({ map: texture, side: THREE.DoubleSide, toneMapped: false }));
  scene.add(mesh);
  heat = { mesh, canvas, ctx: canvas.getContext('2d'), texture, nx: g.nx, ny: g.ny, w, d };
}

function paintHeatmap() {
  const g = last.grid;
  ensureHeatmap(g);
  heat.mesh.position.set((g.x0 + g.x1) / 2, (g.y0 + g.y1) / 2, state.workplane + 0.003);
  const img = heat.ctx.createImageData(g.nx, g.ny);
  for (let j = 0; j < g.ny; ++j) {
    const row = g.ny - 1 - j; // canvas row 0 is the top = largest y
    for (let i = 0; i < g.nx; ++i) {
      const rgb = luxToRgb(last.lux[j * g.nx + i]);
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
  applyViewVisibility();
}

// ---- Whole room: computed light on floor, walls and every object --------------------------

const litGroup = new THREE.Group();
scene.add(litGroup);
const litMat = new THREE.MeshBasicMaterial({ vertexColors: true, toneMapped: false, side: THREE.DoubleSide });
// Patch: { geometry, points, normals (unique evaluation vertices), index (drawn vertex -> unique
// vertex), albedo (linear RGB reflectance colour per drawn vertex) }
let litPatches = [];
let surfaceInfo = '';
let liveWhileDragging = false; // the live work plane is shown while something is dragged
const SURFACE_EDGE = 0.15;     // m: furniture faces are split to this size
const SURFACE_GRID = 0.12;     // m: floor and wall grid spacing
const SURFACE_CHUNK = 25000;   // points per engine call; the page stays responsive between calls
const ALBEDO = { floor: 0xd8d0c2, wall: 0xf1efe9, obstacle: 0xb4b2ab, model: 0xbdbdbd };
const WARM = [1.0, 0.96, 0.88];

const linearRgb = (hex) => { const c = new THREE.Color().setHex(hex); return [c.r, c.g, c.b]; };

function addPatch(geometry, points, normals, index, albedo, label) {
  geometry.setAttribute('color', new THREE.Float32BufferAttribute(new Float32Array(index.length * 3).fill(0.5), 3));
  const patch = { geometry, points, normals, index, albedo, label, lux: null };
  const mesh = new THREE.Mesh(geometry, litMat);
  mesh.userData.patch = patch; // for the hover read-out
  litGroup.add(mesh);
  litPatches.push(patch);
}

/// Illuminance at a hovered point of a whole-room surface: the computed values at the hit
/// triangle's corners, interpolated barycentrically.
function surfaceLuxAt(hit) {
  const patch = hit.object.userData.patch;
  if (!patch?.lux || !hit.face) return null;
  const pos = patch.geometry.attributes.position;
  const corners = [hit.face.a, hit.face.b, hit.face.c];
  const [A, B, C] = corners.map((v) => new THREE.Vector3().fromBufferAttribute(pos, v));
  const w = new THREE.Triangle(A, B, C).getBarycoord(hit.point, new THREE.Vector3());
  const values = corners.map((v) => patch.lux[patch.index[v]]);
  if (values.some(Number.isNaN)) return { label: patch.label, lux: NaN };
  return { label: patch.label, lux: w.x * values[0] + w.y * values[1] + w.z * values[2] };
}

/// Triangles in world coordinates (flat non-indexed) with one sRGB colour per triangle (or one for
/// all) -> tessellated patch; shared vertices are evaluated once.
function addTrianglePatch(world, colors, solid = null, label = 'surface') {
  const big = world.length / 9 > 60000; // big imported models are finely tessellated already
  const perTriangle = Array.isArray(colors);
  const tess = big ? world : tessellate(world, SURFACE_EDGE, 9, perTriangle ? colors : null);
  const triColors = perTriangle ? (big ? colors : tessellate.lastColors) : null;
  const normals = flatNormals(tess);
  const unique = uniqueVertices(evaluationPoints(tess, normals, solid), normals);
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(Float32Array.from(tess), 3));
  const vertices = tess.length / 3;
  const albedo = new Float32Array(vertices * 3);
  const cache = new Map();
  for (let v = 0; v < vertices; ++v) {
    const hex = perTriangle ? triColors[Math.floor(v / 3)] : colors;
    if (!cache.has(hex)) cache.set(hex, linearRgb(hex));
    albedo.set(cache.get(hex), v * 3);
  }
  addPatch(geometry, unique.points, unique.normals, unique.index, albedo, label);
}

/// A flat rectangle as an indexed grid: one evaluation per vertex, smoothly interpolated.
function addGridPatch(width, height, transform, hex, label) {
  const geometry = new THREE.PlaneGeometry(width, height,
    Math.max(1, Math.round(width / SURFACE_GRID)), Math.max(1, Math.round(height / SURFACE_GRID)));
  geometry.applyMatrix4(transform);
  const normals = Float64Array.from(geometry.attributes.normal.array);
  const points = evaluationPoints(Float64Array.from(geometry.attributes.position.array), normals);
  const count = points.length / 3;
  const index = Uint32Array.from({ length: count }, (_, k) => k);
  const albedo = new Float32Array(count * 3);
  const rgb = linearRgb(hex);
  for (let k = 0; k < count; ++k) albedo.set(rgb, k * 3);
  addPatch(geometry, points, normals, index, albedo, label);
}

function buildLit() {
  disposeChildren(litGroup);
  litPatches = [];
  const { width, depth, height } = state.room;
  const T = (x, y, z) => new THREE.Matrix4().makeTranslation(x, y, z);
  addGridPatch(width, depth, T(width / 2, depth / 2, 0), ALBEDO.floor, 'floor');               // facing +z
  addGridPatch(width, height, T(width / 2, depth, height / 2)
    .multiply(new THREE.Matrix4().makeRotationX(Math.PI / 2)), ALBEDO.wall, 'back wall');       // facing -y
  addGridPatch(depth, height, T(width, depth / 2, height / 2)
    .multiply(new THREE.Matrix4().makeRotationZ(-Math.PI / 2))
    .multiply(new THREE.Matrix4().makeRotationX(Math.PI / 2)), ALBEDO.wall, 'side wall');       // facing -x
  for (const mesh of obstacleMeshes) {
    mesh.updateMatrixWorld(true);
    const g = mesh.geometry.index ? mesh.geometry.toNonIndexed() : mesh.geometry.clone();
    g.applyMatrix4(mesh.matrixWorld);
    addTrianglePatch(Float64Array.from(g.attributes.position.array), ALBEDO.obstacle, null, 'obstacle');
    g.dispose();
  }
  for (const item of state.furniture) {
    const isModel = item.type === 'model';
    const local = isModel ? models.get(item.modelId).local : itemGeometry(item.type).positions;
    const colors = isModel ? ALBEDO.model : itemGeometry(item.type).colors;
    const solid = isSolidItem(item) ? placedSolid(CATALOG[item.type].solid, item.x, item.y, item.rotation) : null;
    const label = isModel ? models.get(item.modelId).name : CATALOG[item.type].label.toLowerCase();
    addTrianglePatch(placePoints(local, item.x, item.y, item.rotation), colors, solid, label);
  }
  litDirty = false;
}

/// Linear RGB for a surface: lux mode = the false-colour scale; light appearance = the surface
/// colour times its tone-mapped illuminance (a matte surface looks as bright as ρ·E/π).
function surfaceColor(e, albedo, k) {
  if (Number.isNaN(e)) return linearRgb((NOT_MEASURED[0] << 16) | (NOT_MEASURED[1] << 8) | NOT_MEASURED[2]);
  if (!isAppearance()) {
    const [r, g, b] = luxToRgb(e);
    return linearRgb((r << 16) | (g << 8) | b);
  }
  const tone = 1.35 * Math.pow(1 - Math.exp(-e / (0.45 * state.scaleMax)), 0.85);
  return [0, 1, 2].map((c) => Math.min(1, albedo[k + c] * tone * WARM[c]));
}

/// Evaluates every surface point with the C++ engine (in chunks, so the page stays responsive;
/// a newer request or a drag cancels this one) and colours the meshes.
async function computeSurfaces() {
  if (!isRoomView() || !last) return;
  if (drag) { scheduleSurfaces(); return; }
  const generation = ++surfaceGeneration;
  if (litDirty) buildLit();
  if (geometryDirty) syncGeometry();
  const total = litPatches.reduce((s, p) => s + p.points.length / 3, 0);
  const points = new Float64Array(total * 3), normals = new Float64Array(total * 3);
  let offset = 0;
  for (const p of litPatches) {
    points.set(p.points, offset * 3);
    normals.set(p.normals, offset * 3);
    offset += p.points.length / 3;
  }
  const lux = new Float64Array(total);
  let engineMs = 0;
  for (let start = 0; start < total; start += SURFACE_CHUNK) {
    const end = Math.min(total, start + SURFACE_CHUNK);
    const r = engine.evaluatePoints(points.subarray(start * 3, end * 3), normals.subarray(start * 3, end * 3), state.shadows);
    if (!r.ok) {
      $('error').textContent = `Engine error: ${r.error}`;
      return;
    }
    lux.set(r.lux, start);
    engineMs += r.elapsedMs;
    if (end < total) {
      surfaceInfo = `whole room: computing ${Math.round((100 * end) / total)} %…`;
      await new Promise((resolve) => requestAnimationFrame(resolve));
      if (generation !== surfaceGeneration || drag) return; // superseded
    }
  }
  offset = 0;
  for (const p of litPatches) {
    p.lux = lux.subarray(offset, offset + p.points.length / 3); // for the hover read-out
    const attr = p.geometry.attributes.color;
    for (let v = 0; v < p.index.length; ++v) {
      const [r, g, b] = surfaceColor(lux[offset + p.index[v]], p.albedo, v * 3);
      attr.setXYZ(v, r, g, b);
    }
    attr.needsUpdate = true;
    offset += p.points.length / 3;
  }
  surfaceInfo = `whole room: ${fmt(total)} surface points computed in ${fmt(engineMs, 0)} ms`;
  document.body.dataset.surfaces = String(total); // for automated checks
  liveWhileDragging = false;
  showStats();
  applyViewVisibility();
}

let surfaceTimer = null;
function scheduleSurfaces(delay = 120) {
  clearTimeout(surfaceTimer);
  surfaceTimer = setTimeout(computeSurfaces, delay);
}

/// Whole-room views show the computed surfaces; the work-plane views (and any drag, which needs
/// live feedback) show the work-plane map with the normally shaded furniture.
function applyViewVisibility() {
  const room = isRoomView() && !liveWhileDragging && !litDirty;
  litGroup.visible = room;
  furnitureGroup.visible = !room;
  obstacleGroup.visible = !room;
  for (const child of roomGroup.children) if (child.userData.surface) child.visible = !room;
  if (heat) heat.mesh.visible = !room;
  requestRender();
}

// Lamps: a luminaire body with its real size from the .ldt file (box or disc, emitting
// downward) or a bulb for isotropic sources, suspension wires, a drop line to the work plane,
// and a larger invisible grab handle.
const lampObjects = [];
const bulbMat = new THREE.MeshBasicMaterial({ color: 0xf2b705, toneMapped: false });
const bodyMat = new THREE.MeshStandardMaterial({ color: 0xd6d8db, metalness: 0.6, roughness: 0.35 });
const bodySelectedMat = new THREE.MeshStandardMaterial({ color: 0xeb6834, metalness: 0.2, roughness: 0.5 });
const glowMat = new THREE.MeshBasicMaterial({ color: 0xfff1d0, toneMapped: false, side: THREE.DoubleSide });
const wireMat = new THREE.LineBasicMaterial({ color: 0x6f6e69 });
const dropMat = new THREE.LineDashedMaterial({ color: 0x8a8984, dashSize: 0.06, gapSize: 0.05 });
const handleMat = new THREE.MeshBasicMaterial({ transparent: true, opacity: 0, depthWrite: false });

const dropPoints = (l) => [new THREE.Vector3(l.x, l.y, l.z), new THREE.Vector3(l.x, l.y, state.workplane)];

function luminaireBody(l, selected) {
  const group = new THREE.Group();
  const info = catalog.get(l.lum)?.info;
  if (!info) {
    const bulb = new THREE.Mesh(new THREE.SphereGeometry(0.07, 24, 12), selected ? bodySelectedMat : bulbMat);
    group.add(bulb);
    return { group, size: 0.22 };
  }
  const length = Math.max(info.lengthMm, 60) / 1000, width = Math.max(info.widthMm, 0) / 1000;
  const height = Math.max(info.heightMm, 20) / 1000;
  const mat = selected ? bodySelectedMat : bodyMat;
  if (width <= 0) { // round luminaire: diameter = length
    const body = new THREE.Mesh(new THREE.CylinderGeometry(length / 2, length / 2, height, 32), mat);
    body.rotation.x = Math.PI / 2;
    body.position.z = height / 2;
    const glow = new THREE.Mesh(new THREE.CircleGeometry(length * 0.42, 32), glowMat);
    glow.position.z = -0.001;
    group.add(body, glow);
  } else {
    const body = new THREE.Mesh(new THREE.BoxGeometry(length, width, height), mat);
    body.position.z = height / 2;
    const glow = new THREE.Mesh(new THREE.PlaneGeometry(length * 0.94, width * 0.8), glowMat);
    glow.position.z = -0.001;
    group.add(body, glow);
  }
  // Suspension wires up to the ceiling.
  const top = height, ceiling = state.room.height - l.z;
  const ends = width <= 0 ? [[0, 0]] : [[-length * 0.4, 0], [length * 0.4, 0]];
  for (const [x, y] of ends) {
    group.add(new THREE.Line(new THREE.BufferGeometry().setFromPoints(
      [new THREE.Vector3(x, y, top), new THREE.Vector3(x, y, ceiling)]), wireMat));
  }
  group.rotation.z = THREE.MathUtils.degToRad(l.rotation);
  return { group, size: Math.max(0.22, length / 2) };
}

function rebuildLamps() {
  for (const o of lampObjects) {
    scene.remove(o.group, o.drop);
    o.group.traverse((c) => c.geometry?.dispose());
    o.drop.geometry.dispose();
  }
  lampObjects.length = 0;
  state.lamps.forEach((l, index) => {
    const selected = state.sel?.kind === 'lamp' && state.sel.index === index;
    const { group, size } = luminaireBody(l, selected);
    const handle = new THREE.Mesh(new THREE.SphereGeometry(size, 12, 8), handleMat);
    handle.userData.pick = { kind: 'lamp', index };
    group.add(handle);
    group.position.set(l.x, l.y, l.z);
    const drop = new THREE.Line(new THREE.BufferGeometry().setFromPoints(dropPoints(l)), dropMat);
    drop.computeLineDistances();
    scene.add(group, drop);
    lampObjects.push({ group, handle, drop, data: l });
  });
  syncControls();
  requestRender();
}

/// Moves lamp objects without rebuilding them (used while dragging).
function updateLampPositions() {
  for (const o of lampObjects) {
    o.group.position.set(o.data.x, o.data.y, o.data.z);
    o.drop.geometry.setFromPoints(dropPoints(o.data));
    o.drop.computeLineDistances();
  }
  requestRender();
}

/// Everything that depends on the room, the obstacles, the furniture or the selection.
function rebuildAll() {
  fitToRoom();
  markGeometry();
  buildRoom();
  buildObstacles();
  buildFurniture();
  rebuildLamps();
  scheduleCompute();
}

// ---- Interaction: select and drag ---------------------------------------------------------

const raycaster = new THREE.Raycaster();
const pointer = new THREE.Vector2();
let drag = null; // { kind, index, plane, offset }

function aim(event) {
  const rect = renderer.domElement.getBoundingClientRect();
  pointer.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
  pointer.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;
  raycaster.setFromCamera(pointer, camera);
}

const pickables = () => [...lampObjects.map((o) => o.handle), ...obstacleMeshes, ...furnitureObjects];
function pick() {
  for (const hit of raycaster.intersectObjects(pickables(), true)) {
    if (hit.object.userData.pick) return { hit, ...hit.object.userData.pick };
  }
  return null;
}

function positionOf(kind, index) {
  if (kind === 'lamp') return state.lamps[index];
  if (kind === 'furniture') return state.furniture[index];
  return centerOf(state.obstacles[index]);
}

// Capture phase: runs before OrbitControls, so grabbing an object does not rotate the view.
renderer.domElement.addEventListener('pointerdown', (e) => {
  if (e.button !== 0) return;
  aim(e);
  const found = pick();
  if (!found) return;
  e.stopImmediatePropagation();
  const { kind, index, hit } = found;
  const c = positionOf(kind, index);
  drag = { kind, index, plane: new THREE.Plane(new THREE.Vector3(0, 0, 1), -hit.point.z),
    offset: new THREE.Vector2(c.x - hit.point.x, c.y - hit.point.y) };
  state.sel = { kind, index };
  buildObstacles();
  buildFurniture();
  rebuildLamps();
  if (isRoomView()) { // live feedback while dragging; the whole room is recomputed on release
    liveWhileDragging = true;
    applyViewVisibility();
  }
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
      const tx = p.x + drag.offset.x, ty = p.y + drag.offset.y;
      if (drag.kind === 'lamp') {
        const lamp = state.lamps[drag.index];
        lamp.x = clamp(tx, 0.05, width - 0.05);
        lamp.y = clamp(ty, 0.05, depth - 0.05);
        updateLampPositions();
      } else if (drag.kind === 'furniture') {
        const item = state.furniture[drag.index];
        Object.assign(item, { x: tx, y: ty });
        clampItem(item);
        markGeometry();
        buildFurniture();
      } else {
        const o = state.obstacles[drag.index];
        const half = halfSize(o), c = centerOf(o);
        shift(o, clamp(tx, half.x, width - half.x) - c.x, clamp(ty, half.y, depth - half.y) - c.y);
        markGeometry();
        buildObstacles();
      }
      scheduleCompute();
    }
    tip.style.display = 'none';
    return;
  }
  const over = pick();
  renderer.domElement.style.cursor = over ? 'grab' : '';
  if (litGroup.visible) { // whole-room view: read the computed light of any surface
    const surfaceHit = raycaster.intersectObjects(litGroup.children, false)[0];
    const value = surfaceHit ? surfaceLuxAt(surfaceHit) : null;
    if (value) {
      tip.textContent = `${value.label}: ` + (Number.isNaN(value.lux) ? 'not measured' : `${value.lux.toFixed(0)} lx`);
      tip.style.left = `${e.clientX + 14}px`;
      tip.style.top = `${e.clientY + 14}px`;
      tip.style.display = 'block';
    } else {
      tip.style.display = 'none';
    }
    return;
  }
  const hit = heat && !over ? raycaster.intersectObject(heat.mesh, false)[0] : null;
  if (hit && last) {
    const g = last.grid;
    const i = Math.min(g.nx - 1, Math.floor(hit.uv.x * g.nx));
    const j = Math.min(g.ny - 1, Math.floor(hit.uv.y * g.ny));
    const lux = last.lux[j * g.nx + i];
    tip.textContent = `work plane at x ${hit.point.x.toFixed(2)} m, y ${hit.point.y.toFixed(2)} m: ` +
      (Number.isNaN(lux) ? 'inside an obstacle' : `${lux.toFixed(0)} lx`);
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
  if (renderer.domElement.hasPointerCapture(e.pointerId)) renderer.domElement.releasePointerCapture(e.pointerId);
  renderer.domElement.style.cursor = '';
  if (isRoomView()) scheduleSurfaces(0);
}
renderer.domElement.addEventListener('pointerup', endDrag);
renderer.domElement.addEventListener('pointercancel', endDrag);
renderer.domElement.addEventListener('pointerleave', () => { $('tip').style.display = 'none'; });

// ---- Polar diagram (C0-C180 and C90-C270 planes, nadir at the bottom) ---------------------

const curve = (id, c) => Array.from(engine.polarCurve(id, c, 72)); // γ = 0..180° in 2.5° steps
const selectedLamp = () => (state.sel?.kind === 'lamp' ? state.lamps[state.sel.index] : undefined);
const selectedObstacle = () => (state.sel?.kind === 'obstacle' ? state.obstacles[state.sel.index] : undefined);
const selectedItem = () => (state.sel?.kind === 'furniture' ? state.furniture[state.sel.index] : undefined);

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

/// The tiles show the task areas when the scene has desks or tables (that is where EN 12464-1
/// sets its requirements), otherwise the whole work plane. The whole plane is always in the line
/// below.
function showStats() {
  const task = taskStats(last.lux, last.grid, state.furniture);
  const shown = task ?? { avg: last.avgLux, min: last.minLux, max: last.maxLux, u0: last.uniformityU0 };
  $('stats-title').innerHTML = task
    ? `Task areas <span>(${task.areas} desk working zones and tables, ${fmt(task.n)} points)</span>`
    : 'Work plane';
  $('s-avg').innerHTML = `${fmt(shown.avg)} <small>lx</small>`;
  $('s-min').innerHTML = `${fmt(shown.min)} <small>lx</small>`;
  $('s-max').innerHTML = `${fmt(shown.max)} <small>lx</small>`;
  $('s-u0').textContent = fmt(shown.u0, 2);
  showTaskVerdict(task);
  const total = last.grid.nx * last.grid.ny;
  $('s-meta').textContent = (task ? `Whole work plane: E_avg ${fmt(last.avgLux)} lx, E_min ${fmt(last.minLux)} lx, ` +
    `U0 ${fmt(last.uniformityU0, 2)} (includes small shadow pockets, e.g. right behind chair backs) · ` : '') +
    `${fmt(total)} points (${fmt(total - last.validPoints)} inside obstacles) · ` +
    `${state.lamps.length} luminaires · ${fmt(last.triangles)} shadow triangles · ` +
    `computed in ${fmt(last.elapsedMs, 1)} ms` + (last.buildMs > 0 ? ` (+${fmt(last.buildMs, 1)} ms BVH)` : '') +
    ' · WebAssembly, 1 thread' + (isRoomView() && surfaceInfo ? ` · ${surfaceInfo}` : '');
}


function showTaskVerdict(task) {
  const box = $('task-stats');
  box.hidden = !task;
  if (!task) return;
  const luxOk = task.avg >= TASK_TARGET.lux, u0Ok = task.u0 >= TASK_TARGET.u0;
  box.innerHTML = `Office target (EN 12464-1, indicative): <b>${TASK_TARGET.lux} lx</b> ${luxOk ? '✓' : '✗'} and ` +
    `<b>U0 ≥ ${TASK_TARGET.u0.toFixed(2)}</b> ${u0Ok ? '✓' : '✗'}. ${luxOk && u0Ok ? 'Met.' : 'Not met.'}` +
    (luxOk && u0Ok ? '' : ' This model is pessimistic: direct light only (no reflections from walls and ' +
      'ceiling) and point sources with hard shadows, e.g. from luminaires behind a monitor. Try moving the luminaires.');
}

function updateLegend() {
  const appearance = isAppearance();
  $('legend-bar').classList.toggle('appearance', appearance);
  $('legend-bar').style.background = appearance ? '' : `linear-gradient(to right, ${RAMP.join(', ')})`;
  const ticks = [0, 0.25, 0.5, 0.75, 1].map((t) => `<span>${fmt(t * state.scaleMax)}</span>`);
  ticks[ticks.length - 1] = `<span>${fmt(state.scaleMax)} lx</span>`;
  $('legend-ticks').innerHTML = appearance ? '<span>dark</span><span>bright</span>' : ticks.join('');
}

function fillSelect(select, entries) {
  select.innerHTML = '';
  for (const [value, label] of entries) {
    const option = document.createElement('option');
    option.value = String(value);
    option.textContent = label;
    select.appendChild(option);
  }
}
const fillCatalog = () => fillSelect($('lum'), [...catalog].map(([id, e]) => [id, e.label]));
fillSelect($('fur-type'), Object.entries(CATALOG).map(([type, def]) => [type, def.label]));

function setRange(id, value, text) {
  $(id).value = value;
  $(`${id}-out`).textContent = text;
}

function syncControls() {
  const lamp = selectedLamp(), obstacle = selectedObstacle(), item = selectedItem();
  $('lamp-section').hidden = !!(obstacle || item);
  $('obstacle-section').hidden = !obstacle;
  $('furniture-section').hidden = !item;
  for (const id of ['lum', 'cd', 'dim', 'rot', 'apply-all']) $(id).disabled = !lamp;
  $('remove').disabled = !(lamp || obstacle || item);
  $('sel-label').textContent = lamp ? `#${state.sel.index + 1} of ${state.lamps.length}` : '(none: click a luminaire)';
  if (lamp) {
    $('lum').value = String(lamp.lum);
    setRange('cd', lamp.cd, `${fmt(lamp.cd)} cd`);
    setRange('dim', Math.round(lamp.dimming * 100), `${Math.round(lamp.dimming * 100)} %`);
    setRange('rot', lamp.rotation, `${lamp.rotation}°`);
  }
  const photometric = lamp && lamp.lum !== ISOTROPIC;
  $('row-cd').hidden = !!photometric;
  $('row-dim').hidden = !photometric;
  $('row-rot').hidden = !(photometric && catalog.get(lamp.lum)?.info?.symmetry !== 1);

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
  if (item) {
    const solid = isSolidItem(item);
    $('fur-label').textContent = item.type === 'model' ? models.get(item.modelId).name : CATALOG[item.type].label;
    $('frot').step = solid ? 90 : 15;
    setRange('frot', item.rotation, `${item.rotation}°`);
    $('fur-note').textContent = 'Drag it in the 3D view to move it.' +
      (solid ? ' Closed furniture counts as a solid block for the calculation, so it turns in 90° steps.' : '') +
      (item.type === 'model' ? ` ${fmt(models.get(item.modelId).local.length / 9)} triangles.` : '');
  }

  $('wp').max = maxWorkplane();
  setRange('wp', state.workplane, `${state.workplane.toFixed(2)} m`);
  $('cell').value = String(state.cell);
  $('shadows').checked = state.shadows;
  $('view-mode').value = state.view;
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
  rebuildLamps();
  scheduleCompute();
});
$('lum').addEventListener('change', (e) => {
  const lamp = selectedLamp();
  if (!lamp) return;
  lamp.lum = Number(e.target.value);
  rebuildLamps();
  scheduleCompute();
});
onLamp('cd', (l, v) => { l.cd = v; });
onLamp('dim', (l, v) => { l.dimming = v / 100; });
onLamp('rot', (l, v) => { l.rotation = v; });
$('apply-all').addEventListener('click', () => {
  const lamp = selectedLamp();
  if (!lamp) return;
  for (const l of state.lamps) Object.assign(l, { lum: lamp.lum, cd: lamp.cd, dimming: lamp.dimming, rotation: lamp.rotation });
  rebuildLamps();
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
    if (!selectedLamp() && state.lamps.length) state.sel = { kind: 'lamp', index: 0 };
    if (selectedLamp()) selectedLamp().lum = id;
    rebuildLamps();
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
  markGeometry();
  buildObstacles();
  syncControls();
  scheduleCompute();
});
onObstacle('bw', (o, v) => { resizeBox(o, v, o.y1 - o.y0); });
onObstacle('bd', (o, v) => { resizeBox(o, o.x1 - o.x0, v); });
onObstacle('bh', (o, v) => { o.h = v; });
onObstacle('sr', (o, v) => { o.r = v; });
onObstacle('sz', (o, v) => { o.z = v; });

// Furniture controls.
$('frot').addEventListener('input', (e) => {
  const item = selectedItem();
  if (!item) return;
  item.rotation = Number(e.target.value);
  clampItem(item);
  markGeometry();
  buildFurniture();
  syncControls();
  scheduleCompute();
});
$('add-fur').addEventListener('click', () => {
  const type = $('fur-type').value;
  state.furniture.push({ type, x: state.room.width / 2, y: state.room.depth / 2, rotation: 0 });
  state.sel = { kind: 'furniture', index: state.furniture.length - 1 };
  rebuildAll();
});

/// Imports a glTF (.glb / embedded .gltf) or OBJ model. Y-up models are turned upright, very
/// large or tiny ones are scaled to about 1.5 m, and the model is centred on the floor. The same
/// triangles are drawn and sent to the engine.
$('model-file').addEventListener('change', async (e) => {
  const file = e.target.files[0];
  e.target.value = '';
  if (!file) return;
  try {
    const buffer = await file.arrayBuffer();
    let root;
    if (/\.obj$/i.test(file.name)) {
      const { OBJLoader } = await import('three/addons/loaders/OBJLoader.js');
      root = new OBJLoader().parse(new TextDecoder().decode(buffer));
    } else {
      const { GLTFLoader } = await import('three/addons/loaders/GLTFLoader.js');
      root = (await new GLTFLoader().parseAsync(buffer, '')).scene;
    }
    root.rotation.x = Math.PI / 2; // glTF and most OBJ files are Y-up; LumaCore is Z-up
    root.updateMatrixWorld(true);
    const box = new THREE.Box3().setFromObject(root);
    const size = box.getSize(new THREE.Vector3());
    const largest = Math.max(size.x, size.y, size.z);
    if (!(largest > 0)) throw new Error('the model has no geometry');
    const scale = largest > 4 || largest < 0.05 ? 1.5 / largest : 1;
    root.scale.setScalar(scale);
    root.updateMatrixWorld(true);
    box.setFromObject(root);
    root.position.set(-(box.min.x + box.max.x) / 2, -(box.min.y + box.max.y) / 2, -box.min.z);
    root.updateMatrixWorld(true);

    const coords = [];
    const v = new THREE.Vector3();
    root.traverse((o) => {
      if (!o.isMesh) return;
      const pos = o.geometry.attributes.position;
      const index = o.geometry.index;
      const count = index ? index.count : pos.count;
      for (let k = 0; k + 2 < count; k += 3) {
        for (let m = 0; m < 3; ++m) {
          v.fromBufferAttribute(pos, index ? index.getX(k + m) : k + m).applyMatrix4(o.matrixWorld);
          coords.push(v.x, v.y, v.z);
        }
      }
    });
    if (coords.length / 9 > MAX_MODEL_TRIANGLES) {
      throw new Error(`${fmt(coords.length / 9)} triangles; the limit is ${fmt(MAX_MODEL_TRIANGLES)}`);
    }
    const local = Float64Array.from(coords);
    const bounds = new THREE.Box3().setFromObject(root);
    const id = nextModelId++;
    models.set(id, { name: file.name, object: root, local,
      bounds: [bounds.min.x, bounds.min.y, bounds.min.z, bounds.max.x, bounds.max.y, bounds.max.z] });
    state.furniture.push({ type: 'model', modelId: id, x: state.room.width / 2, y: state.room.depth / 2, rotation: 0 });
    state.sel = { kind: 'furniture', index: state.furniture.length - 1 };
    notice = '';
    rebuildAll();
  } catch (err) {
    $('error').textContent = `Could not import ${file.name}: ${err.message}`;
  }
});

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
  updateLampPositions();
  scheduleCompute();
});
$('cell').addEventListener('change', (e) => { state.cell = Number(e.target.value); scheduleCompute(); });
$('shadows').addEventListener('change', (e) => { state.shadows = e.target.checked; scheduleCompute(); });
$('view-mode').addEventListener('change', (e) => {
  state.view = VIEWS.includes(e.target.value) ? e.target.value : 'room-appearance';
  if (last) paintHeatmap();
  if (isRoomView()) scheduleSurfaces(0);
  showStats();
});
$('preset').addEventListener('change', (e) => {
  loadScene(e.target.value);
  history.replaceState(null, '', `${window.location.pathname}?preset=${e.target.value}`);
  notice = '';
  rebuildAll();
  frameRoom();
});

$('add').addEventListener('click', () => {
  const template = selectedLamp() ?? newLamp(0, 0);
  state.lamps.push({ ...template, x: state.room.width / 2, y: state.room.depth / 2 });
  state.sel = { kind: 'lamp', index: state.lamps.length - 1 };
  rebuildAll();
});
$('add-box').addEventListener('click', () => {
  const cx = state.room.width / 2, cy = state.room.depth / 2;
  state.obstacles.push({ kind: 'box', x0: cx - 0.3, x1: cx + 0.3, y0: cy - 0.3, y1: cy + 0.3, h: 1.2 });
  state.sel = { kind: 'obstacle', index: state.obstacles.length - 1 };
  rebuildAll();
});
$('remove').addEventListener('click', () => {
  if (selectedLamp()) state.lamps.splice(state.sel.index, 1);
  else if (selectedObstacle()) state.obstacles.splice(state.sel.index, 1);
  else if (selectedItem()) state.furniture.splice(state.sel.index, 1);
  state.sel = null;
  rebuildAll();
});
$('reset').addEventListener('click', () => {
  loadScene($('preset').value);
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
  for (const item of state.furniture) {
    if (isSolidItem(item)) lines.push(`# box,${placedSolid(CATALOG[item.type].solid, item.x, item.y, item.rotation).join(',')}`);
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
  let dropped = false;
  const lamps = state.lamps.map((l) => {
    const key = catalog.get(l.lum)?.key;
    if (!key) dropped = true; // uploaded .ldt files are too large for a link
    return [r3(l.x), r3(l.y), key ?? 'iso', l.cd, r3(l.dimming), l.rotation];
  });
  const obstacles = state.obstacles.map((o) => (o.kind === 'box'
    ? ['b', r3(o.x0), r3(o.y0), r3(o.x1), r3(o.y1), r3(o.h)]
    : ['s', r3(o.x), r3(o.y), r3(o.z), r3(o.r)]));
  const furniture = state.furniture.filter((item) => {
    if (item.type === 'model') dropped = true; // imported models are too large for a link
    return item.type !== 'model';
  }).map((item) => [item.type, r3(item.x), r3(item.y), item.rotation]);
  const design = { v: 1, room: [state.room.width, state.room.depth, state.room.height],
    wp: state.workplane, cell: state.cell, sh: state.shadows ? 1 : 0, view: state.view, lamps, obstacles, furniture };
  return { hash: `#s=${toBase64Url(JSON.stringify(design))}`, dropped };
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
  state.view = VIEWS.includes(design.view) ? design.view : 'room-appearance';
  state.lamps = (design.lamps ?? []).slice(0, 100).map(([x, y, key, cd, dimming, rotation]) => newLamp(num(x, 1), num(y, 1), {
    lum: idByKey[key] ?? ISOTROPIC,
    cd: clamp(num(cd, 800), 0, 100000),
    dimming: clamp(num(dimming, 1), 0, 1.5),
    rotation: clamp(num(rotation, 0), 0, 180),
  }));
  state.obstacles = (design.obstacles ?? []).slice(0, 100).map((o) => {
    if (o[0] === 'b') {
      const x0 = num(o[1], 1), y0 = num(o[2], 1);
      return { kind: 'box', x0, y0, x1: Math.max(num(o[3], x0 + 0.5), x0 + 0.05),
        y1: Math.max(num(o[4], y0 + 0.5), y0 + 0.05), h: clamp(num(o[5], 1), 0.05, 5) };
    }
    return { kind: 'sphere', x: num(o[1], 1), y: num(o[2], 1), z: num(o[3], 1), r: clamp(num(o[4], 0.25), 0.05, 2) };
  });
  state.furniture = (design.furniture ?? []).slice(0, 200)
    .filter(([type]) => type in CATALOG)
    .map(([type, x, y, rotation]) => {
      const rot = num(rotation, 0);
      return { type, x: num(x, 1), y: num(y, 1),
        rotation: CATALOG[type].solid ? Math.round(rot / 90) * 90 % 360 : ((rot % 360) + 360) % 360 };
    });
  state.sel = state.lamps.length ? { kind: 'lamp', index: 0 } : null;
}

$('share').addEventListener('click', async () => {
  const { hash, dropped } = encodeDesign();
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
    (dropped ? ' Uploaded .ldt files and imported models are not included.' : '');
});

// ---- Start --------------------------------------------------------------------------------

window.addEventListener('resize', () => {
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight);
  requestRender();
});

// Initial design: ?preset=reference|furnished, then a share link (#s=...), then
// ?luminaire=linear_batwing&rotation=90&dimming=80 (applied to every lamp).
const params = new URLSearchParams(window.location.search);
loadScene(params.get('preset') ?? 'furnished');
try {
  if (window.location.hash.startsWith('#s=')) decodeDesign(window.location.hash);
} catch (err) {
  notice = `Could not read the share link (${err.message}); showing the default scene.`;
  loadScene(params.get('preset') ?? 'furnished');
}
if (VIEWS.includes(params.get('view'))) state.view = params.get('view');
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
requestRender();

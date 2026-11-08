// LumaCore Web: interactive 3D front end for the C++ engine compiled to WebAssembly.
// Coordinates are the engine's: metres, right-handed, +Z up.

import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import createLumaCore from './lumacore.js';

// ---- Scenario (same office as native/apps/common/scenarios.cpp) -------------------------

const ROOM = { width: 6, depth: 4, height: 3 };
const BORDER = 0.5; // excluded band along the walls (EN 12464-1 border zone)
const LAMP_Z = 2.8;
const ISOTROPIC = -1; // luminaire id of an isotropic point source
const newLamp = (x, y) => ({ x, y, z: LAMP_Z, lum: ISOTROPIC, cd: 800, dimming: 1, rotation: 0 });
const DEFAULT_LAMPS = [[1, 1], [3, 1], [5, 1], [1, 3], [3, 3], [5, 3]].map(([x, y]) => newLamp(x, y));
const BOXES = [
  { min: [0.05, 2.6, 0.0], max: [0.65, 3.95, 2.0] }, // cabinet
  { min: [2.9, 0.2, 0.0], max: [3.1, 1.6, 1.6] },    // partition
];
const SPHERES = [{ center: [4.5, 2.6, 1.9], radius: 0.25 }]; // pendant globe
const SAMPLE_LDT = ['downlight_wide.ldt', 'spot_narrow.ldt', 'linear_batwing.ldt'];

// Sequential single-hue ramp, light (low lux) -> dark (high lux); same as tools/heatmap.py.
const RAMP = ['#cde2fb', '#b7d3f6', '#9ec5f4', '#86b6ef', '#6da7ec', '#5598e7', '#3987e5',
  '#2a78d6', '#256abf', '#1c5cab', '#184f95', '#104281', '#0d366b'];
const NOT_MEASURED = [0xc9, 0xc8, 0xc2];

const state = {
  lamps: DEFAULT_LAMPS.map((l) => ({ ...l })),
  selected: 0,
  workplane: 0.75,
  cell: 0.05,
  shadows: true,
  scaleMax: 600, // colour scale top [lx]; grows if exceeded, never jumps while dragging
};

// ---- Helpers ------------------------------------------------------------------------------

const $ = (id) => document.getElementById(id);
const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
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
  const x0 = BORDER, y0 = BORDER, x1 = ROOM.width - BORDER, y1 = ROOM.depth - BORDER;
  return {
    x0, y0, x1, y1,
    nx: Math.max(1, Math.round((x1 - x0) / state.cell)),
    ny: Math.max(1, Math.round((y1 - y0) / state.cell)),
  };
}

// ---- Engine -------------------------------------------------------------------------------

let lc, engine;
let last = null; // { nx, ny, lux: Float64Array, ... } of the latest successful run

try {
  lc = await createLumaCore();
} catch (err) {
  $('loading').textContent = `Could not load the WebAssembly engine: ${err}`;
  throw err;
}
engine = new lc.Engine();
engine.setRoom(ROOM.width, ROOM.depth, ROOM.height);
for (const b of BOXES) engine.addBox(...b.min, ...b.max);
for (const s of SPHERES) engine.addSphere(...s.center, s.radius);

// Luminaire catalogue: id -> { label, info }. The isotropic point source is built in.
const catalog = new Map([[ISOTROPIC, { label: 'Isotropic point source (bare lamp)', info: null }]]);

function addPhotometry(text, label) {
  const id = engine.addPhotometry(text);
  if (id < 0) throw new Error(engine.lastError());
  const info = engine.photometryInfo(id);
  catalog.set(id, { label: label ?? info.name, info });
  return id;
}

const sampleIds = {}; // 'linear_batwing' -> photometry id
for (const file of SAMPLE_LDT) { // sequential: stable ids and menu order
  try {
    const response = await fetch(`ldt/${file}`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    sampleIds[file.replace(/\.ldt$/i, '')] = addPhotometry(await response.text());
  } catch (err) {
    console.warn(`sample ${file} not loaded: ${err.message}`);
  }
}

// Shareable links: ?luminaire=linear_batwing&rotation=90&dimming=80 sets every lamp.
const params = new URLSearchParams(window.location.search);
if (params.get('luminaire') in sampleIds) {
  const lum = sampleIds[params.get('luminaire')];
  const rotation = clamp(Number(params.get('rotation') ?? 0) || 0, 0, 180);
  const dimming = clamp(Number(params.get('dimming') ?? 100) || 100, 0, 150) / 100;
  for (const l of DEFAULT_LAMPS) Object.assign(l, { lum, rotation, dimming });
  state.lamps = DEFAULT_LAMPS.map((l) => ({ ...l }));
}

function compute() {
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
  $('error').textContent = '';
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
const camera = new THREE.PerspectiveCamera(42, window.innerWidth / window.innerHeight, 0.05, 100);
camera.up.set(0, 0, 1);
camera.position.set(-0.1, -5.2, 8.2); // high enough to read the work plane, low enough for depth
const controls = new OrbitControls(camera, renderer.domElement);
controls.target.set(2.4, 2.0, 0.7); // left of the room centre, so the panel does not cover it
controls.enableDamping = true;
controls.maxPolarAngle = Math.PI * 0.49;
controls.update();

scene.add(new THREE.HemisphereLight(0xffffff, 0xd8d6d0, 2.2));
const sun = new THREE.DirectionalLight(0xffffff, 1.6);
sun.position.set(-3, -5, 9);
scene.add(sun);

// Room: floor, two far walls (translucent) and the outline.
const floor = new THREE.Mesh(new THREE.PlaneGeometry(ROOM.width, ROOM.depth),
  new THREE.MeshStandardMaterial({ color: 0xf0efec }));
floor.position.set(ROOM.width / 2, ROOM.depth / 2, 0);
scene.add(floor);
const wallMat = new THREE.MeshStandardMaterial({ color: 0xf4f3ef, transparent: true, opacity: 0.45,
  side: THREE.DoubleSide, depthWrite: false });
const backWall = new THREE.Mesh(new THREE.PlaneGeometry(ROOM.width, ROOM.height), wallMat);
backWall.rotation.x = Math.PI / 2;
backWall.position.set(ROOM.width / 2, ROOM.depth, ROOM.height / 2);
const sideWall = new THREE.Mesh(new THREE.PlaneGeometry(ROOM.depth, ROOM.height), wallMat);
sideWall.rotation.set(Math.PI / 2, Math.PI / 2, 0);
sideWall.position.set(ROOM.width, ROOM.depth / 2, ROOM.height / 2);
scene.add(backWall, sideWall);
const outline = new THREE.LineSegments(
  new THREE.EdgesGeometry(new THREE.BoxGeometry(ROOM.width, ROOM.depth, ROOM.height)),
  new THREE.LineBasicMaterial({ color: 0x8a8984 }));
outline.position.set(ROOM.width / 2, ROOM.depth / 2, ROOM.height / 2);
scene.add(outline);

// Obstacles.
const obstacleMat = new THREE.MeshStandardMaterial({ color: 0xb4b2ab, roughness: 0.8 });
const edgeMat = new THREE.LineBasicMaterial({ color: 0x52514e });
for (const b of BOXES) {
  const size = [0, 1, 2].map((k) => b.max[k] - b.min[k]);
  const geo = new THREE.BoxGeometry(...size);
  const mesh = new THREE.Mesh(geo, obstacleMat);
  const edges = new THREE.LineSegments(new THREE.EdgesGeometry(geo), edgeMat);
  const centre = [0, 1, 2].map((k) => (b.min[k] + b.max[k]) / 2);
  mesh.position.set(...centre);
  edges.position.set(...centre);
  scene.add(mesh, edges);
}
for (const s of SPHERES) {
  const mesh = new THREE.Mesh(new THREE.SphereGeometry(s.radius, 40, 20), obstacleMat);
  mesh.position.set(...s.center);
  scene.add(mesh);
}

// Heatmap on the work plane: a canvas texture with one pixel per grid cell.
let heat = null; // { mesh, canvas, ctx, texture, nx, ny }

function ensureHeatmap(g) {
  if (heat && heat.nx === g.nx && heat.ny === g.ny) return;
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
  const mesh = new THREE.Mesh(new THREE.PlaneGeometry(g.x1 - g.x0, g.y1 - g.y0),
    new THREE.MeshBasicMaterial({ map: texture, side: THREE.DoubleSide }));
  scene.add(mesh);
  heat = { mesh, canvas, ctx: canvas.getContext('2d'), texture, nx: g.nx, ny: g.ny };
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

function dropPoints(l) {
  return [new THREE.Vector3(l.x, l.y, l.z), new THREE.Vector3(l.x, l.y, state.workplane)];
}

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
    o.group.position.set(o.data.x, o.data.y, o.data.z);
    o.bulb.material = index === state.selected ? bulbSelectedMat : bulbMat;
    o.bulb.scale.setScalar(index === state.selected ? 1.35 : 1);
    o.axis.visible = isAsymmetric(o.data);
    o.axis.rotation.z = THREE.MathUtils.degToRad(o.data.rotation);
    o.drop.geometry.setFromPoints(dropPoints(o.data));
    o.drop.computeLineDistances();
  });
  syncControls();
}

// ---- Interaction --------------------------------------------------------------------------

const raycaster = new THREE.Raycaster();
const pointer = new THREE.Vector2();
let dragging = null;

function aim(event) {
  const rect = renderer.domElement.getBoundingClientRect();
  pointer.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
  pointer.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;
  raycaster.setFromCamera(pointer, camera);
}

const handles = () => lampObjects.map((o) => o.handle);

// Capture phase: runs before OrbitControls, so grabbing a lamp does not rotate the view.
renderer.domElement.addEventListener('pointerdown', (e) => {
  if (e.button !== 0) return;
  aim(e);
  const hit = raycaster.intersectObjects(handles(), false)[0];
  if (!hit) return;
  e.stopImmediatePropagation();
  dragging = hit.object.userData.index;
  state.selected = dragging;
  updateLampMeshes();
  renderer.domElement.setPointerCapture(e.pointerId);
  renderer.domElement.style.cursor = 'grabbing';
}, { capture: true });

renderer.domElement.addEventListener('pointermove', (e) => {
  aim(e);
  const tip = $('tip');
  if (dragging !== null) {
    const lamp = state.lamps[dragging];
    const plane = new THREE.Plane(new THREE.Vector3(0, 0, 1), -lamp.z);
    const p = new THREE.Vector3();
    if (raycaster.ray.intersectPlane(plane, p)) {
      lamp.x = clamp(p.x, 0.05, ROOM.width - 0.05);
      lamp.y = clamp(p.y, 0.05, ROOM.depth - 0.05);
      updateLampMeshes();
      scheduleCompute();
    }
    tip.style.display = 'none';
    return;
  }
  const overLamp = raycaster.intersectObjects(handles(), false).length > 0;
  renderer.domElement.style.cursor = overLamp ? 'grab' : '';
  const hit = heat && !overLamp ? raycaster.intersectObject(heat.mesh, false)[0] : null;
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
  if (dragging === null) return;
  dragging = null;
  if (renderer.domElement.hasPointerCapture(e.pointerId)) {
    renderer.domElement.releasePointerCapture(e.pointerId);
  }
  renderer.domElement.style.cursor = '';
}
renderer.domElement.addEventListener('pointerup', endDrag);
renderer.domElement.addEventListener('pointercancel', endDrag);
renderer.domElement.addEventListener('pointerleave', () => { $('tip').style.display = 'none'; });

// ---- Polar diagram (C0-C180 and C90-C270 planes, nadir at the bottom) ---------------------

function curve(id, c) {
  return Array.from(engine.polarCurve(id, c, 72)); // γ = 0..180° in 2.5° steps; copy the view
}

function drawPolar() {
  const canvas = $('polar');
  const ctx = canvas.getContext('2d');
  const w = canvas.width, h = canvas.height;
  ctx.clearRect(0, 0, w, h);
  const lamp = state.lamps[state.selected];
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

  // Grid: circles at 25 % steps of the peak, vertical and horizontal axes.
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
    `computed in ${fmt(last.elapsedMs, 1)} ms (WebAssembly, 1 thread)`;
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

function syncControls() {
  const lamp = state.lamps[state.selected];
  for (const id of ['lum', 'cd', 'dim', 'rot', 'remove', 'apply-all']) $(id).disabled = !lamp;
  $('sel-label').textContent = lamp ? `#${state.selected + 1} of ${state.lamps.length}` : '(none: click a lamp)';
  if (lamp) {
    $('lum').value = String(lamp.lum);
    $('cd').value = lamp.cd;
    $('dim').value = Math.round(lamp.dimming * 100);
    $('rot').value = lamp.rotation;
  }
  const photometric = lamp && lamp.lum !== ISOTROPIC;
  $('row-cd').hidden = !!photometric;
  $('row-dim').hidden = !photometric;
  $('row-rot').hidden = !(lamp && isAsymmetric(lamp));
  $('cd-out').textContent = lamp ? `${fmt(lamp.cd)} cd` : '';
  $('dim-out').textContent = lamp ? `${Math.round(lamp.dimming * 100)} %` : '';
  $('rot-out').textContent = lamp ? `${lamp.rotation}°` : '';
  $('wp').value = state.workplane;
  $('wp-out').textContent = `${state.workplane.toFixed(2)} m`;
  $('cell').value = String(state.cell);
  $('shadows').checked = state.shadows;
  drawPolar();
}

const selectedLamp = () => state.lamps[state.selected];

$('lum').addEventListener('change', (e) => {
  const lamp = selectedLamp();
  if (!lamp) return;
  lamp.lum = Number(e.target.value);
  updateLampMeshes();
  scheduleCompute();
});
$('cd').addEventListener('input', (e) => {
  const lamp = selectedLamp();
  if (!lamp) return;
  lamp.cd = Number(e.target.value);
  syncControls();
  scheduleCompute();
});
$('dim').addEventListener('input', (e) => {
  const lamp = selectedLamp();
  if (!lamp) return;
  lamp.dimming = Number(e.target.value) / 100;
  syncControls();
  scheduleCompute();
});
$('rot').addEventListener('input', (e) => {
  const lamp = selectedLamp();
  if (!lamp) return;
  lamp.rotation = Number(e.target.value);
  updateLampMeshes();
  scheduleCompute();
});
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
    if (!selectedLamp()) state.selected = 0;
    if (selectedLamp()) selectedLamp().lum = id;
    $('error').textContent = '';
    updateLampMeshes();
    scheduleCompute();
  } catch (err) {
    $('error').textContent = `Could not read ${file.name}: ${err.message}`;
  }
});
$('wp').addEventListener('input', (e) => {
  state.workplane = Number(e.target.value);
  updateLampMeshes();
  scheduleCompute();
});
$('cell').addEventListener('change', (e) => { state.cell = Number(e.target.value); scheduleCompute(); });
$('shadows').addEventListener('change', (e) => { state.shadows = e.target.checked; scheduleCompute(); });
$('add').addEventListener('click', () => {
  const template = selectedLamp() ?? newLamp(0, 0);
  state.lamps.push({ ...template, x: ROOM.width / 2, y: ROOM.depth / 2 });
  state.selected = state.lamps.length - 1;
  rebuildLamps();
  scheduleCompute();
});
$('remove').addEventListener('click', () => {
  if (!selectedLamp()) return;
  state.lamps.splice(state.selected, 1);
  state.selected = -1;
  rebuildLamps();
  scheduleCompute();
});
$('reset').addEventListener('click', () => {
  state.lamps = DEFAULT_LAMPS.map((l) => ({ ...l }));
  Object.assign(state, { selected: 0, workplane: 0.75, cell: 0.05, shadows: true, scaleMax: 600 });
  rebuildLamps();
  scheduleCompute();
});

// ---- Start --------------------------------------------------------------------------------

window.addEventListener('resize', () => {
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight);
});

$('version').textContent = `engine ${lc.version()}`;
$('loading').remove();
$('panel').hidden = false;
fillCatalog();
rebuildLamps();
compute();

renderer.setAnimationLoop(() => {
  controls.update();
  renderer.render(scene, camera);
});

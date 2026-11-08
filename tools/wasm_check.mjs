// Checks the WebAssembly build (web/lumacore.js) in Node.js:
//   1. closed form: 1000 cd at 2 m above the only cell gives 250 lx,
//   2. invalid input returns { ok: false, error } instead of throwing,
//   3. writes the office grid to results/office_wasm.csv for tools/compare_grids.py.
//
// Usage (from the repository root, Node 18+):
//   node tools/wasm_check.mjs

import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const { default: createLumaCore } = await import(pathToFileURL(join(root, 'web', 'lumacore.js')).href);
const lc = await createLumaCore();

let failures = 0;
function check(name, condition, detail = '') {
  console.log(`${condition ? '[ok]  ' : '[FAIL]'} ${name}${detail ? `  (${detail})` : ''}`);
  if (!condition) failures += 1;
}

console.log(`LumaCore WebAssembly engine ${lc.version()}`);

// 1. Closed form.
{
  const engine = new lc.Engine();
  engine.addLight(0, 0, 2, 1000);
  const r = engine.simulate(-0.5, -0.5, 0.5, 0.5, 0, 1, 1, true);
  check('E = I / h^2 = 250 lx', r.ok && Math.abs(r.lux[0] - 250) < 1e-12, r.ok ? `got ${r.lux[0]}` : r.error);
  engine.delete();
}

// 2. Errors become values, not exceptions.
{
  const engine = new lc.Engine();
  engine.addLight(0, 0, 2, -5);
  const r = engine.simulate(-0.5, -0.5, 0.5, 0.5, 0, 1, 1, true);
  check('negative intensity is reported', !r.ok && /intensity/.test(r.error), r.error);
  const r2 = engine.simulate(0, 0, 1, 1, 0, 0, 1, true);
  check('nx = 0 is reported', !r2.ok && /nx/.test(r2.error), r2.error);
  engine.delete();
}

// 3. Office scenario, same as native/apps/common/scenarios.cpp with a 5 cm grid.
{
  const engine = new lc.Engine();
  engine.setRoom(6, 4, 3);
  for (const y of [1, 3]) for (const x of [1, 3, 5]) engine.addLight(x, y, 2.8, 800);
  engine.addBox(0.05, 2.6, 0.0, 0.65, 3.95, 2.0);
  engine.addBox(2.9, 0.2, 0.0, 3.1, 1.6, 1.6);
  engine.addSphere(4.5, 2.6, 1.9, 0.25);
  const [x0, y0, x1, y1, z, nx, ny] = [0.5, 0.5, 5.5, 3.5, 0.75, 100, 60];
  const r = engine.simulate(x0, y0, x1, y1, z, nx, ny, true);
  check('office simulation runs', r.ok, r.ok ? `E_avg ${r.avgLux.toFixed(1)} lx, ${r.elapsedMs.toFixed(1)} ms` : r.error);

  if (r.ok) {
    const lines = [
      `# lumacore,${lc.version()}`, '# scenario,office (WebAssembly)', '# shadows,1',
      '# room,6,4,3', `# grid,${nx},${ny},${x0},${y0},${x1},${y1},${z}`,
      `# stats,${r.minLux},${r.maxLux},${r.avgLux},${r.uniformityU0},${r.validPoints}`,
      'i,j,x_m,y_m,z_m,lux',
    ];
    const dx = (x1 - x0) / nx, dy = (y1 - y0) / ny;
    for (let j = 0; j < ny; ++j) {
      for (let i = 0; i < nx; ++i) {
        const e = r.lux[j * nx + i];
        // String(number) is the shortest text that parses back to the same double.
        lines.push(`${i},${j},${x0 + dx * (i + 0.5)},${y0 + dy * (j + 0.5)},${z},${Number.isNaN(e) ? 'nan' : String(e)}`);
      }
    }
    mkdirSync(join(root, 'results'), { recursive: true });
    writeFileSync(join(root, 'results', 'office_wasm.csv'), lines.join('\n') + '\n');
    console.log('wrote results/office_wasm.csv (compare: python tools/compare_grids.py results/office.csv results/office_wasm.csv)');
  }
  engine.delete();
}

if (failures > 0) {
  console.error(`${failures} check(s) failed`);
  process.exit(1);
}
console.log('all WebAssembly checks passed');

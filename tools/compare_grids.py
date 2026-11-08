#!/usr/bin/env python3
"""Compare two LumaCore grid CSVs (e.g. C++ CLI vs C# console) value by value.

Usage:
    python tools/compare_grids.py results/office.csv results/office_csharp.csv
    python tools/compare_grids.py a.csv b.csv --rel-tol 1e-12

Exit code 0 only if both have the same shape, the same not-measured (NaN) cells and values that
are exactly equal (default) or within the given relative tolerance. Both writers use 17
significant digits, so doubles round-trip exactly.

Why a tolerance exists: photometric lights convert directions to angles with atan2/acos. C++
does not require these to be correctly rounded, and MSVC's and Emscripten's maths libraries can
differ in the last bit. Isotropic scenes use no trigonometry and must match exactly.
"""

import sys
from pathlib import Path

import numpy as np


def load(path: Path) -> np.ndarray:
    header = sum(1 for line in path.open(encoding="utf-8") if line.startswith("#"))
    rows = np.genfromtxt(path, delimiter=",", skip_header=header, names=True)
    nx, ny = int(rows["i"].max()) + 1, int(rows["j"].max()) + 1
    grid = np.full((ny, nx), np.nan)
    grid[rows["j"].astype(int), rows["i"].astype(int)] = rows["lux"]
    return grid


def main(argv) -> int:
    rel_tol = 0.0
    if "--rel-tol" in argv:
        k = argv.index("--rel-tol")
        rel_tol = float(argv[k + 1])
        argv = argv[:k] + argv[k + 2:]
    if len(argv) != 3:
        print(__doc__)
        return 2
    a, b = load(Path(argv[1])), load(Path(argv[2]))
    if a.shape != b.shape:
        print(f"shape differs: {a.shape} vs {b.shape}")
        return 1
    nan_a, nan_b = np.isnan(a), np.isnan(b)
    if not np.array_equal(nan_a, nan_b):
        print(f"not-measured cells differ: {nan_a.sum()} vs {nan_b.sum()}")
        return 1
    diff = np.abs(a[~nan_a] - b[~nan_b])
    rel = diff / np.maximum(np.abs(a[~nan_a]), 1e-300)
    print(f"{a.size} cells, {nan_a.sum()} not measured, {(diff == 0).sum()} exactly equal, "
          f"max abs diff {diff.max():.3g} lx, max rel diff {rel.max():.3g}")
    if rel_tol > 0.0:
        print(f"tolerance: relative {rel_tol:g}")
        return 0 if rel.max() <= rel_tol else 1
    return 0 if diff.max() == 0.0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))

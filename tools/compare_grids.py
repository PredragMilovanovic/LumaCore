#!/usr/bin/env python3
"""Compare two LumaCore grid CSVs (e.g. C++ CLI vs C# console) value by value.

Usage:
    python tools/compare_grids.py results/office.csv results/office_csharp.csv

Exit code 0 only if both have the same shape, the same not-measured (NaN) cells and exactly
equal values. Both writers use 17 significant digits, so doubles round-trip exactly.
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
    return 0 if diff.max() == 0.0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))

#!/usr/bin/env python3
"""Plot a LumaCore illuminance grid (CSV written by lumacore_cli) as an isolux heatmap.

Usage:
    python tools/heatmap.py results/office.csv [-o results/office.png]

The CSV header lines starting with '#' carry the room, grid, luminaires, obstacles and
statistics; the data rows are i,j,x_m,y_m,z_m,lux ('nan' = point inside an obstacle).
"""

import argparse
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")  # file output only, no window

import matplotlib.patheffects as pe  # noqa: E402
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
from matplotlib.colors import LinearSegmentedColormap  # noqa: E402
from matplotlib.lines import Line2D  # noqa: E402
from matplotlib.patches import Circle, Patch, Rectangle  # noqa: E402
from matplotlib.ticker import MaxNLocator  # noqa: E402

# Sequential single-hue ramp, light (low lux) -> dark (high lux).
BLUE_RAMP = [
    "#cde2fb", "#b7d3f6", "#9ec5f4", "#86b6ef", "#6da7ec", "#5598e7", "#3987e5",
    "#2a78d6", "#256abf", "#1c5cab", "#184f95", "#104281", "#0d366b",
]
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK_SECONDARY = "#52514e"
NEUTRAL = "#f0efec"       # room area outside the calculation grid (border zone)
NOT_MEASURED = "#c9c8c2"  # cells inside obstacles

HALO = [pe.withStroke(linewidth=2.6, foreground=SURFACE)]


def read_grid(path: Path):
    """Return (metadata dict, lux array of shape (ny, nx) with NaN for excluded cells)."""
    meta = {"light": [], "box": [], "sphere": []}
    header_lines = 0
    with path.open(encoding="utf-8") as f:
        for line in f:
            if not line.startswith("#"):
                break
            header_lines += 1
            key, *values = line[1:].strip().split(",")
            if key in ("light", "box", "sphere"):
                meta[key].append([float(v) for v in values])
            elif key in ("room", "grid", "stats"):
                meta[key] = [float(v) for v in values]
            else:
                meta[key] = values[0] if values else ""

    rows = np.genfromtxt(path, delimiter=",", skip_header=header_lines, names=True)
    nx, ny = int(meta["grid"][0]), int(meta["grid"][1])
    lux = np.full((ny, nx), np.nan)
    lux[rows["j"].astype(int), rows["i"].astype(int)] = rows["lux"]
    return meta, lux


def plot(meta, lux, out_path: Path):
    width, depth, _height = meta["room"]
    nx, ny, gx0, gy0, gx1, gy1, plane_z = meta["grid"]
    e_min, e_max, e_avg, u0 = meta["stats"][:4]

    fig_w = 10.0
    fig, ax = plt.subplots(figsize=(fig_w, fig_w * depth / width * 0.85 + 2.4),
                           layout="constrained", facecolor=SURFACE)
    ax.set_facecolor(SURFACE)

    # Room floor plan; the band outside the calculation grid is the excluded border zone.
    ax.add_patch(Rectangle((0, 0), width, depth, facecolor=NEUTRAL, edgecolor=INK,
                           linewidth=1.5, zorder=0))

    cmap = LinearSegmentedColormap.from_list("lumacore_blue", BLUE_RAMP)
    cmap.set_bad(NOT_MEASURED)
    levels = [v for v in MaxNLocator(nbins=6).tick_values(0.0, np.nanmax(lux)) if v > 0]
    vmax = max(levels[-1], np.nanmax(lux))
    masked = np.ma.masked_invalid(lux)
    image = ax.imshow(masked, origin="lower", extent=(gx0, gx1, gy0, gy1), cmap=cmap,
                      vmin=0.0, vmax=vmax, interpolation="nearest", zorder=1)

    # Isolux lines through the cell centers.
    xs = gx0 + (np.arange(int(nx)) + 0.5) * (gx1 - gx0) / nx
    ys = gy0 + (np.arange(int(ny)) + 0.5) * (gy1 - gy0) / ny
    contours = ax.contour(xs, ys, masked, levels=levels, colors=INK, linewidths=0.8, zorder=2)
    contours.set(path_effects=HALO)
    labels = ax.clabel(contours, fmt=lambda v: f"{v:g}", fontsize=8, inline=True)
    for label in labels:
        label.set_color(INK)
        label.set_path_effects(HALO)

    # Calculation area outline.
    ax.add_patch(Rectangle((gx0, gy0), gx1 - gx0, gy1 - gy0, fill=False,
                           edgecolor=INK_SECONDARY, linewidth=1.0, linestyle=":", zorder=3))

    # Obstacle footprints (sphere: largest horizontal cross-section).
    for x0, y0, _z0, x1, y1, _z1 in meta["box"]:
        ax.add_patch(Rectangle((x0, y0), x1 - x0, y1 - y0, fill=False, edgecolor=INK,
                               linewidth=1.2, linestyle="--", zorder=4))
    for cx, cy, _cz, r in meta["sphere"]:
        ax.add_patch(Circle((cx, cy), r, fill=False, edgecolor=INK, linewidth=1.2,
                            linestyle="--", zorder=4))

    # Luminaires: the usual plan symbol, a circle with a cross.
    for x, y, _z, _cd in meta["light"]:
        for marker, size in (("o", 12), ("x", 7)):
            ax.plot(x, y, marker=marker, markersize=size, markerfacecolor="none",
                    markeredgecolor=INK, markeredgewidth=1.6, zorder=5, path_effects=HALO)

    colorbar = fig.colorbar(image, ax=ax, shrink=0.9, pad=0.02, ticks=levels)
    colorbar.set_label("Illuminance E [lx]", color=INK)
    colorbar.outline.set_edgecolor(INK_SECONDARY)
    colorbar.ax.tick_params(colors=INK_SECONDARY)

    shadows = "on" if meta.get("shadows", "1") == "1" else "off"
    fig.suptitle(
        f"Direct illuminance on the work plane at {plane_z:g} m: '{meta.get('scenario', '')}'\n"
        f"E_avg {e_avg:.0f} lx   E_min {e_min:.0f} lx   E_max {e_max:.0f} lx   "
        f"U0 = E_min / E_avg = {u0:.2f}   shadows {shadows}",
        color=INK, fontsize=11, x=0.02, ha="left")
    ax.set_xlabel("x [m]", color=INK_SECONDARY)
    ax.set_ylabel("y [m]", color=INK_SECONDARY)
    ax.tick_params(colors=INK_SECONDARY)
    for spine in ax.spines.values():
        spine.set_visible(False)
    margin = 0.15
    ax.set_xlim(-margin, width + margin)
    ax.set_ylim(-margin, depth + margin)
    ax.set_aspect("equal")

    key = [
        Line2D([], [], marker="o", markersize=9, markerfacecolor="none",
               markeredgecolor=INK, linestyle="none", label="luminaire"),
        Line2D([], [], color=INK, linewidth=0.8, label="isolux line [lx]"),
        Line2D([], [], color=INK, linewidth=1.2, linestyle="--", label="obstacle footprint"),
        Patch(facecolor=NOT_MEASURED, label="inside obstacle (not measured)"),
        Patch(facecolor=NEUTRAL, edgecolor=INK_SECONDARY, linestyle=":",
              label="border zone (excluded)"),
    ]
    # Figure-level legend placed "outside" so constrained layout reserves room for it.
    legend = fig.legend(handles=key, loc="outside lower center", ncol=3, frameon=False,
                        fontsize=9)
    for text in legend.get_texts():
        text.set_color(INK_SECONDARY)

    fig.savefig(out_path, dpi=150, facecolor=SURFACE)
    plt.close(fig)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("csv", type=Path, help="grid CSV written by lumacore_cli")
    parser.add_argument("-o", "--out", type=Path, help="output PNG (default: next to the CSV)")
    args = parser.parse_args(argv)

    if not args.csv.is_file():
        print(f"error: {args.csv} not found (run lumacore_cli first)", file=sys.stderr)
        return 1
    out = args.out or args.csv.with_suffix(".png")
    meta, lux = read_grid(args.csv)
    plot(meta, lux, out)
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

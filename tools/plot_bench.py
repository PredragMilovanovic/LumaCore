#!/usr/bin/env python3
"""Plot LumaCore benchmark results (CSV written by lumacore_bench): speedup and efficiency.

Usage:
    python tools/plot_bench.py results/bench.csv [-o results/bench.png] [--physical-cores 12]
"""

import argparse
import csv
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt  # noqa: E402

SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK_SECONDARY = "#52514e"
INK_MUTED = "#8a8984"
GRID = "#e4e3df"
SERIES = "#2a78d6"  # categorical slot 1 (single series)


def read_rows(path: Path):
    with path.open(encoding="utf-8", newline="") as f:
        rows = [r for r in csv.DictReader(f) if r["backend"] == "threaded"]
    if not rows:
        raise ValueError(f"{path} has no 'threaded' rows")
    rows.sort(key=lambda r: int(r["threads"]))
    meta = {"points": int(rows[0]["points"]), "lights": int(rows[0]["lights"])}
    threads = [int(r["threads"]) for r in rows]
    speedup = [float(r["speedup"]) for r in rows]
    efficiency = [100.0 * float(r["efficiency"]) for r in rows]
    identical = all(r["identical_to_sequential"] == "1" for r in rows)
    return meta, threads, speedup, efficiency, identical


def style_axis(ax, xmax):
    ax.set_facecolor(SURFACE)
    ax.grid(True, color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(INK_MUTED)
    ax.tick_params(colors=INK_SECONDARY)
    ax.set_xlim(0, xmax + 1)
    ax.set_xlabel("threads", color=INK_SECONDARY)


def mark_cores(ax, cores, y_text):
    ax.axvline(cores, color=INK_MUTED, linewidth=1.0, linestyle=":", zorder=1)
    ax.text(cores + 0.4, y_text, f"{cores} physical cores", color=INK_SECONDARY, fontsize=9,
            va="top")


def plot(meta, threads, speedup, efficiency, identical, cores, out: Path):
    xmax = max(threads)
    fig, (ax_s, ax_e) = plt.subplots(1, 2, figsize=(11, 4.6), layout="constrained",
                                     facecolor=SURFACE)
    line = dict(color=SERIES, linewidth=2, marker="o", markersize=8,
                markeredgecolor=SURFACE, markeredgewidth=2, zorder=3)

    # Speedup with the ideal (linear) reference.
    style_axis(ax_s, xmax)
    ax_s.plot([0, xmax], [0, xmax], color=INK_MUTED, linewidth=1.2, linestyle="--", zorder=2)
    ax_s.text(xmax * 0.62, xmax * 0.62 + 0.8, "ideal (linear)", color=INK_SECONDARY,
              fontsize=9, rotation=0)
    ax_s.plot(threads, speedup, **line)
    ax_s.set_ylim(0, xmax + 1)
    ax_s.set_ylabel("speedup vs sequential  [x]", color=INK_SECONDARY)
    ax_s.set_title("Speedup", color=INK, loc="left", fontsize=11)
    mark_cores(ax_s, cores, xmax * 0.97)
    ax_s.annotate(f"{speedup[-1]:.1f}x", (threads[-1], speedup[-1]), xytext=(0, 10),
                  textcoords="offset points", ha="center", color=INK, fontsize=9)

    # Parallel efficiency = speedup / threads.
    style_axis(ax_e, xmax)
    ax_e.plot(threads, efficiency, **line)
    ax_e.set_ylim(0, 110)
    ax_e.set_ylabel("parallel efficiency  [%]", color=INK_SECONDARY)
    ax_e.set_title("Efficiency (speedup / threads)", color=INK, loc="left", fontsize=11)
    mark_cores(ax_e, cores, 107)

    check = "bit-identical to sequential: yes" if identical else "RESULTS DIFFER FROM SEQUENTIAL"
    fig.suptitle(
        f"LumaCore ThreadedExecutor - office scenario, {meta['points']:,} points, "
        f"{meta['lights']} lights, shadows on   ({check})",
        color=INK, fontsize=11, x=0.01, ha="left")
    fig.savefig(out, dpi=150, facecolor=SURFACE)
    plt.close(fig)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("csv", type=Path)
    parser.add_argument("-o", "--out", type=Path)
    parser.add_argument("--physical-cores", type=int, default=12)
    args = parser.parse_args(argv)
    if not args.csv.is_file():
        print(f"error: {args.csv} not found (run lumacore_bench first)", file=sys.stderr)
        return 1
    out = args.out or args.csv.with_suffix(".png")
    plot(*read_rows(args.csv), args.physical_cores, out)
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

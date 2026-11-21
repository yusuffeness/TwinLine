#!/usr/bin/env python3
"""Plot TwinLine benchmark results (CSV from bench_analysis) as a PNG/SVG chart.

Two panels share the thread axis:
  * left:  median analysis time per mode (log scale)
  * right: speed-up over one thread, with the ideal linear speed-up for reference

Usage:
    python3 bench/plot.py bench/results/benchmark.csv -o docs/assets/benchmark.png
"""

from __future__ import annotations

import argparse
import csv
import platform
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.ticker import FuncFormatter  # noqa: E402

# Validated categorical palette (slots 1 and 2) and neutral inks.
SERIES = {"pruned": "#2a78d6", "exhaustive": "#eb6834"}
LABELS = {"pruned": "Pruned (default)", "exhaustive": "Exhaustive"}
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK_SECONDARY = "#52514e"
GRID = "#e4e3df"


def cpu_name() -> str:
    try:
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or platform.machine()


def load(path: Path, corpus: str | None):
    rows = list(csv.DictReader(path.open(encoding="utf-8")))
    if not rows:
        raise SystemExit(f"{path}: no data")
    corpus = corpus or rows[0]["corpus"]
    series: dict[str, list[tuple[int, float, float]]] = defaultdict(list)
    for row in rows:
        if row["corpus"] == corpus:
            series[row["mode"]].append(
                (int(row["threads"]), float(row["seconds"]), float(row["speedup"]))
            )
    for points in series.values():
        points.sort()
    return corpus, series


def nice_ticks(values: list[float]) -> list[float]:
    """1-2-5 ticks covering the data, for a logarithmic seconds axis."""
    lo, hi = min(values), max(values)
    ticks = []
    decade = 0.001
    while decade <= hi * 10:
        for step in (1, 2, 5):
            tick = step * decade
            if lo / 1.5 <= tick <= hi * 1.5:
                ticks.append(round(tick, 6))
        decade *= 10
    return ticks


def style_axis(ax) -> None:
    ax.set_facecolor(SURFACE)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(GRID)
    ax.tick_params(which="both", colors=INK_SECONDARY, labelsize=9, length=0)
    ax.grid(True, color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)


def plot(corpus: str, series, output: Path, title: str | None) -> None:
    plt.rcParams.update({"font.family": "DejaVu Sans", "text.color": INK})
    fig, (left, right) = plt.subplots(1, 2, figsize=(10, 4.0), facecolor=SURFACE)
    threads = sorted({t for points in series.values() for t, _, _ in points})

    for mode in ("pruned", "exhaustive"):
        points = series.get(mode)
        if not points:
            continue
        xs = [t for t, _, _ in points]
        secs = [s for _, s, _ in points]
        speed = [p for _, _, p in points]
        style = dict(color=SERIES[mode], linewidth=2, marker="o", markersize=7,
                     markeredgecolor=SURFACE, markeredgewidth=2, label=LABELS[mode])
        left.plot(xs, secs, **style)
        right.plot(xs, speed, **style)
        # Direct labels at the end of each line.
        left.annotate(f"{secs[-1]:.2f} s", (xs[-1], secs[-1]), xytext=(8, 0),
                      textcoords="offset points", va="center", fontsize=9, color=INK)
        right.annotate(f"{speed[-1]:.1f}x", (xs[-1], speed[-1]), xytext=(8, 0),
                       textcoords="offset points", va="center", fontsize=9, color=INK)

    right.plot(threads, threads, color=INK_SECONDARY, linewidth=1.2, linestyle=(0, (4, 3)),
               label="Ideal (linear)")

    for ax in (left, right):
        style_axis(ax)
        ax.set_xscale("log", base=2)
        ax.set_xticks(threads)
        ax.xaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{int(v)}"))
        ax.set_xlabel("Worker threads", fontsize=9, color=INK_SECONDARY)
        ax.margins(x=0.12)

    # Both panels use log scales, so equal ratios look equal and the ideal
    # linear speed-up is a straight diagonal.
    left.set_yscale("log")
    left.set_yticks(nice_ticks([s for pts in series.values() for _, s, _ in pts]))
    left.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:g} s"))
    left.yaxis.set_minor_formatter(FuncFormatter(lambda v, _: ""))
    right.set_yscale("log", base=2)
    right.set_yticks(threads)
    right.yaxis.set_minor_formatter(FuncFormatter(lambda v, _: ""))
    left.set_title("Median analysis time (lower is better)", fontsize=10, loc="left", color=INK)
    right.set_title("Speed-up over one thread", fontsize=10, loc="left", color=INK)
    right.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:g}x"))
    right.legend(frameon=False, fontsize=9, loc="upper left", labelcolor=INK)

    fig.suptitle(title or f"TwinLine near-duplicate analysis: {corpus}", x=0.01, y=0.975,
                 ha="left", fontsize=12, fontweight="bold", color=INK)
    fig.text(0.01, 0.895, f"{cpu_name()}, median of repeated runs", fontsize=9,
             color=INK_SECONDARY, ha="left")
    fig.subplots_adjust(left=0.07, right=0.95, bottom=0.13, top=0.78, wspace=0.28)

    output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output, dpi=200, facecolor=SURFACE)
    fig.savefig(output.with_suffix(".svg"), facecolor=SURFACE)
    print(f"wrote {output} and {output.with_suffix('.svg')}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("csv", type=Path, help="CSV written by bench_analysis --csv")
    parser.add_argument("-o", "--output", type=Path, default=Path("docs/assets/benchmark.png"))
    parser.add_argument("--corpus", help="corpus name to plot (default: the first one)")
    parser.add_argument("--title", help="chart title")
    args = parser.parse_args()
    corpus, series = load(args.csv, args.corpus)
    plot(corpus, series, args.output, args.title)


if __name__ == "__main__":
    main()

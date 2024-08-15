#!/usr/bin/env python3
"""Render the before/after battery-life bar chart (dependency-free SVG) from
the JSON written by `compare_designs.py --json`."""
from __future__ import annotations

import json
import sys

LABELS = {"polling": "polling (before)", "fifo": "+ FIFO batching + tickless", "wom": "+ wake-on-motion gating"}
COLORS = {"polling": "#8a8f98", "fifo": "#3b7ddd", "wom": "#1b9e77"}


def svg(rows: list[dict]) -> str:
    w, h, left, top, bar_h, gap = 760, 60 + 62 * len(rows) + 50, 230, 60, 38, 24
    vmax = max(r["report"]["battery_years"] for r in rows) * 1.25
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" font-family="sans-serif" font-size="13">',
           '<rect width="100%" height="100%" fill="#fff"/>',
           f'<text x="{w / 2}" y="26" text-anchor="middle" font-size="16" font-weight="bold">'
           'Estimated battery life by firmware design (225 mAh cell, assumed current model)</text>']
    for i, r in enumerate(rows):
        y = top + i * (bar_h + gap)
        yrs, ua = r["report"]["battery_years"], r["report"]["avg_current_na"] / 1000
        bw = (w - left - 120) * yrs / vmax
        d = r["design"]
        out.append(f'<text x="{left - 10}" y="{y + bar_h / 2 + 4}" text-anchor="end">{LABELS.get(d, d)}</text>')
        out.append(f'<rect x="{left}" y="{y}" width="{max(bw, 2):.1f}" height="{bar_h}" fill="{COLORS.get(d, "#555")}"/>')
        out.append(f'<text x="{left + bw + 8:.1f}" y="{y + bar_h / 2 + 4}">{yrs:.2f} years ({ua:.0f} µA avg)</text>')
    out.append(f'<text x="{left}" y="{h - 14}" fill="#555">Same simulated scenario for all designs; '
               'currents are assumed, not bench-measured.</text></svg>')
    return "\n".join(out) + "\n"


if __name__ == "__main__":
    rows = json.load(open(sys.argv[1]))
    open(sys.argv[2], "w").write(svg(rows))

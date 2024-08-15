#!/usr/bin/env python3
"""Run every firmware design on the same simulated scenario and compare
battery life, energy breakdown and detection quality against ground truth.

    python tools/compare_designs.py --duration 600 [--scenario day] [--seed 7]
"""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import decode_log  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent
DESIGNS = ["polling", "fifo", "wom"]
TOLERANCE_S = 2.5          # detection matches truth if its start is this close


def match_events(truth: list[dict], events: list[dict], tol: float = TOLERANCE_S) -> dict:
    """Greedy one-to-one matching on (class, time)."""
    used: set[int] = set()
    tp = 0
    for t in truth:
        best, best_d = None, tol
        for i, e in enumerate(events):
            if i in used or e["event_class"] != t["cls"]:
                continue
            # an event "covers" the truth interval if it starts within tol of it
            d = max(0.0, t["t_start"] - e["timestamp_ms"] / 1000.0, e["timestamp_ms"] / 1000.0 - t["t_end"])
            if d <= best_d:
                best, best_d = i, d
        if best is not None:
            used.add(best)
            tp += 1
    fp, fn = len(events) - len(used), len(truth) - tp
    return {"tp": tp, "fp": fp, "fn": fn,
            "recall": tp / len(truth) if truth else 1.0,
            "precision": tp / len(events) if events else 1.0}


def run_design(binary: str, design: str, scenario: str, seed: int, duration: float, workdir: pathlib.Path):
    log, rep, truth = (workdir / f"{design}.{x}" for x in ("log", "json", "truth.json"))
    cmd = [binary, "--design", design, "--scenario", scenario, "--seed", str(seed),
           "--duration", str(duration), "--log", str(log), "--report", str(rep),
           "--truth", str(truth), "--python", sys.executable]
    subprocess.run(cmd, check=True, timeout=900, env={**os.environ})
    report = json.loads(rep.read_text())
    gt = json.loads(truth.read_text())["events"]
    events = [r["event"] for r in decode_log.load(str(log)) if "event" in r]
    return report, gt, events


def render(rows: list[dict]) -> str:
    hdr = (f"{'design':<8} {'avg uA':>9} {'years':>7} {'wakes':>9} {'sleep%':>7} {'mcu-busy%':>9} "
           f"{'sensor%':>8} {'radio%':>7} {'recall':>7} {'prec':>6}")
    lines = [hdr, "-" * len(hdr)]
    for r in rows:
        st = r["report"]["states"]
        busy = sum(st[k]["share_pct"] for k in ("idle", "wake", "sample", "inference"))
        lines.append(
            f"{r['design']:<8} {r['report']['avg_current_na'] / 1000:9.1f} {r['report']['battery_years']:7.2f} "
            f"{r['report']['wakes']:9d} {st['sleep']['share_pct']:7.1f} {busy:9.1f} "
            f"{r['report']['sensor_share_pct']:8.1f} {st['radio']['share_pct']:7.2f} "
            f"{r['match']['recall']:7.2f} {r['match']['precision']:6.2f}")
    return "\n".join(lines)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--binary", default=str(ROOT / "build" / "microamp"))
    ap.add_argument("--scenario", default="day")
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--duration", type=float, default=600.0)
    ap.add_argument("--designs", nargs="+", default=DESIGNS)
    ap.add_argument("--json", help="also write results as JSON to this path")
    a = ap.parse_args(argv)
    rows = []
    with tempfile.TemporaryDirectory() as td:
        for d in a.designs:
            report, gt, events = run_design(a.binary, d, a.scenario, a.seed, a.duration, pathlib.Path(td))
            rows.append({"design": d, "report": report, "truth_events": len(gt), "detected": len(events),
                         "match": match_events(gt, events)})
    print(f"scenario={a.scenario} seed={a.seed} duration={a.duration:.0f}s "
          f"truth_events={rows[0]['truth_events']} battery={rows[0]['report']['battery_mah']} mAh")
    print(render(rows))
    base = rows[0]["report"]["battery_years"] or 1e-9
    print("\nbattery-life gain vs", rows[0]["design"] + ":",
          ", ".join(f"{r['design']} x{r['report']['battery_years'] / base:.1f}" for r in rows[1:]))
    if a.json:
        pathlib.Path(a.json).write_text(json.dumps(rows, indent=1) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())

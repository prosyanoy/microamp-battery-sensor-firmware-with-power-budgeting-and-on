#!/usr/bin/env python3
"""One-command demo: replay a synthetic drive through the firmware (wom design),
print the detected events next to the ground truth and the projected battery life."""
from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import decode_log  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--duration", type=float, default=240.0)
    ap.add_argument("--seed", type=int, default=3)
    ap.add_argument("--design", default="wom")
    a = ap.parse_args()
    with tempfile.TemporaryDirectory() as td:
        td = pathlib.Path(td)
        subprocess.run([str(ROOT / "build" / "microamp"), "--design", a.design, "--scenario", "drive",
                        "--duration", str(a.duration), "--seed", str(a.seed), "--log", str(td / "l"),
                        "--report", str(td / "r.json"), "--truth", str(td / "t.json"),
                        "--status-period", "60", "--python", sys.executable], check=True, timeout=300)
        recs, rep = decode_log.load(str(td / "l")), json.loads((td / "r.json").read_text())
        truth = json.loads((td / "t.json").read_text())["events"]
    print(f"Replayed {a.duration:.0f} s synthetic drive (seed {a.seed}, design '{a.design}')\n")
    print("ground truth:")
    for t in truth:
        print(f"  {t['t_start']:7.2f}s  {t['cls']}")
    print("\ndetected (decoded from the nanopb log):")
    for r in recs:
        if "event" in r:
            print("  " + decode_log.format_record(r))
    print(f"\nprojected battery life: {rep['battery_years']:.2f} years on {rep['battery_mah']} mAh "
          f"(avg {rep['avg_current_na'] / 1000:.1f} µA; assumed current model, not bench-measured)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

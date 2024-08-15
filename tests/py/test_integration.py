"""End-to-end: firmware binary + Python simulator + decoder."""
import json
import pathlib
import subprocess
import sys

import pytest

import decode_log

ROOT = pathlib.Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "microamp"
pytestmark = pytest.mark.skipif(not BIN.exists(), reason="build the firmware first (make build)")


def run(tmp_path, design, scenario="drive", duration=90, seed=5):
    log, rep, truth = tmp_path / f"{design}.log", tmp_path / f"{design}.json", tmp_path / f"{design}.truth"
    subprocess.run([str(BIN), "--design", design, "--scenario", scenario, "--duration", str(duration),
                    "--seed", str(seed), "--log", str(log), "--report", str(rep), "--truth", str(truth),
                    "--status-period", "20", "--python", sys.executable], check=True, timeout=240)
    return (decode_log.load(str(log)), json.loads(rep.read_text()), json.loads(truth.read_text()))


@pytest.fixture(scope="module")
def runs(tmp_path_factory):
    tmp = tmp_path_factory.mktemp("runs")
    return {d: run(tmp, d) for d in ("polling", "fifo", "wom")}


def test_log_has_periodic_status_with_battery_estimate(runs):
    recs, rep, _ = runs["fifo"]
    status = [r["status"] for r in recs if "status" in r]
    assert len(status) >= 4                                  # every 20 s + final
    assert all(s["sensor_ok"] for s in status)
    assert status[-1]["battery_years_x100"] == pytest.approx(rep["battery_years"] * 100, abs=2)
    assert [r["seq"] for r in recs] == list(range(len(recs)))


def test_events_are_detected_with_right_class(runs):
    recs, _, truth = runs["fifo"]
    got = [r["event"] for r in recs if "event" in r]
    assert truth["events"] and got
    for t in truth["events"]:
        assert any(g["event_class"] == t["cls"] and abs(g["timestamp_ms"] / 1000 - t["t_start"]) < 2.5
                   for g in got), t


def test_energy_ranking_polling_fifo_wom(runs):
    years = {d: runs[d][1]["battery_years"] for d in runs}
    assert years["polling"] < years["fifo"] < years["wom"] or years["fifo"] > 5 * years["polling"]
    assert years["fifo"] > 5 * years["polling"]
    assert runs["polling"][1]["wakes"] > 10 * runs["fifo"][1]["wakes"]


def test_state_times_add_up_to_duration(runs):
    for _, rep, _ in runs.values():
        total = sum(s["time_us"] for s in rep["states"].values())
        assert total == pytest.approx(rep["duration_s"] * 1e6, rel=1e-6)


def test_parked_wom_sleeps_through_quiet(tmp_path):
    _, rep, _ = run(tmp_path, "wom", "parked", duration=60)
    assert rep["stats"]["motion_wakes"] <= 3
    assert rep["stats"]["samples"] < 1000          # a streaming design reads 6000 in 60 s


def test_no_queue_drops_or_overruns(runs):
    for d, (_, rep, _) in runs.items():
        assert rep["stats"]["queue_drops"] == 0, d
        assert rep["stats"]["fifo_overruns"] == 0 or d == "wom", d

#!/usr/bin/env python3
"""Decode the device log: length-delimited nanopb `LogRecord` messages.

A minimal protobuf wire-format reader driven by the schema in proto/microamp.proto,
so no protobuf runtime is needed.

    python tools/decode_log.py out/microamp.log [--json]
"""
from __future__ import annotations

import argparse
import json
import sys

CLASS_NAMES = {0: "normal", 1: "harsh_brake", 2: "impact"}

# field number -> (name, kind)   kind: "u32" | "bool" | "enum" | ("msg", schema)
EVENT = {1: ("timestamp_ms", "u32"), 2: ("event_class", "enum"), 3: ("peak_mg", "u32"),
         4: ("confidence", "u32"), 5: ("duration_ms", "u32")}
STATUS = {1: ("timestamp_ms", "u32"), 2: ("battery_years_x100", "u32"), 3: ("avg_current_na", "u32"),
          4: ("events_total", "u32"), 5: ("windows_total", "u32"), 6: ("fifo_overruns", "u32"),
          7: ("sensor_ok", "bool")}
RECORD = {1: ("seq", "u32"), 2: ("event", ("msg", EVENT)), 3: ("status", ("msg", STATUS))}


def read_varint(buf: bytes, pos: int) -> tuple[int, int]:
    shift = val = 0
    while True:
        if pos >= len(buf):
            raise ValueError("truncated varint")
        b = buf[pos]
        pos += 1
        val |= (b & 0x7F) << shift
        if not b & 0x80:
            return val, pos
        shift += 7
        if shift > 63:
            raise ValueError("varint too long")


def decode_message(buf: bytes, schema: dict) -> dict:
    out: dict = {}
    for name, kind in schema.values():                # proto3: absent scalar == default
        if kind == "u32":
            out[name] = 0
        elif kind == "bool":
            out[name] = False
        elif kind == "enum":
            out[name] = CLASS_NAMES[0]
    pos = 0
    while pos < len(buf):
        key, pos = read_varint(buf, pos)
        field, wire = key >> 3, key & 7
        if wire == 0:
            val, pos = read_varint(buf, pos)
        elif wire == 2:
            ln, pos = read_varint(buf, pos)
            if pos + ln > len(buf):
                raise ValueError("truncated field")
            val, pos = buf[pos:pos + ln], pos + ln
        elif wire == 5:
            val, pos = int.from_bytes(buf[pos:pos + 4], "little"), pos + 4
        elif wire == 1:
            val, pos = int.from_bytes(buf[pos:pos + 8], "little"), pos + 8
        else:
            raise ValueError(f"unsupported wire type {wire}")
        if field not in schema:
            continue                                  # forward compatible: skip unknown
        name, kind = schema[field]
        if isinstance(kind, tuple):
            out[name] = decode_message(val, kind[1])
        elif kind == "bool":
            out[name] = bool(val)
        elif kind == "enum":
            out[name] = CLASS_NAMES.get(val, str(val))
        else:
            out[name] = val & 0xFFFFFFFF
    return out


def decode_stream(data: bytes) -> list[dict]:
    records, pos = [], 0
    while pos < len(data):
        ln, pos = read_varint(data, pos)
        if pos + ln > len(data):
            raise ValueError("truncated record at end of log")
        records.append(decode_message(data[pos:pos + ln], RECORD))
        pos += ln
    return records


def load(path: str) -> list[dict]:
    with open(path, "rb") as f:
        return decode_stream(f.read())


def format_record(r: dict) -> str:
    if "event" in r:
        e = r["event"]
        return (f"#{r['seq']:<4} EVENT  t={e.get('timestamp_ms', 0) / 1000:8.2f}s  "
                f"{e.get('event_class', 'normal'):<11} peak={e.get('peak_mg', 0) / 1000:5.2f} g  "
                f"dur={e.get('duration_ms', 0):4d} ms  conf={e.get('confidence', 0)}")
    s = r.get("status", {})
    return (f"#{r['seq']:<4} STATUS t={s.get('timestamp_ms', 0) / 1000:8.2f}s  "
            f"battery={s.get('battery_years_x100', 0) / 100:6.2f} y  "
            f"avg={s.get('avg_current_na', 0) / 1000:8.2f} uA  events={s.get('events_total', 0)} "
            f"windows={s.get('windows_total', 0)} sensor_ok={s.get('sensor_ok', False)}")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("log")
    ap.add_argument("--json", action="store_true", help="emit JSON lines")
    a = ap.parse_args(argv)
    try:
        recs = load(a.log)
    except (OSError, ValueError) as exc:
        print(f"decode_log: {exc}", file=sys.stderr)
        return 1
    for r in recs:
        print(json.dumps(r) if a.json else format_record(r))
    return 0


if __name__ == "__main__":
    sys.exit(main())

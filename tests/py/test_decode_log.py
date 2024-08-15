import pytest

import decode_log as D


def varint(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def frame(msg: bytes) -> bytes:
    return varint(len(msg)) + msg


EVENT_REC = bytes([0x08, 0x05, 0x12, 0x0D, 0x08, 0xE8, 0x07, 0x10, 0x02, 0x18, 0xB8, 0x17, 0x20, 0x40, 0x28, 0x96, 0x01])
STATUS_REC = bytes([0x08, 0x01, 0x1A, 0x07, 0x10, 0xD2, 0x09, 0x38, 0x01, 0x20, 0x03])


def test_decode_event_record():
    (r,) = D.decode_stream(frame(EVENT_REC))
    assert r["seq"] == 5
    assert r["event"] == {"timestamp_ms": 1000, "event_class": "impact", "peak_mg": 3000,
                          "confidence": 64, "duration_ms": 150}


def test_decode_status_and_multiple_records():
    recs = D.decode_stream(frame(EVENT_REC) + frame(STATUS_REC))
    assert [("event" in r) for r in recs] == [True, False]
    s = recs[1]["status"]
    assert s["battery_years_x100"] == 1234 and s["sensor_ok"] is True


def test_proto3_defaults_filled():
    (r,) = D.decode_stream(frame(bytes([0x12, 0x02, 0x10, 0x01])))   # seq absent, class=brake
    assert r["seq"] == 0 and r["event"]["event_class"] == "harsh_brake" and r["event"]["peak_mg"] == 0


def test_unknown_fields_are_skipped():
    msg = EVENT_REC + bytes([0x78, 0x01])                           # field 15 varint
    assert D.decode_stream(frame(msg))[0]["seq"] == 5


def test_truncated_log_is_an_error():
    with pytest.raises(ValueError):
        D.decode_stream(frame(EVENT_REC)[:-3])


def test_format_record_is_readable():
    (r,) = D.decode_stream(frame(EVENT_REC))
    line = D.format_record(r)
    assert "impact" in line and "3.00 g" in line

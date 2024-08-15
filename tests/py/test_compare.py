import compare_designs as C


def ev(cls, t_s):
    return {"event_class": cls, "timestamp_ms": int(t_s * 1000)}


TRUTH = [{"cls": "impact", "t_start": 10.0, "t_end": 10.2},
         {"cls": "harsh_brake", "t_start": 50.0, "t_end": 51.0}]


def test_perfect_match():
    m = C.match_events(TRUTH, [ev("impact", 10.4), ev("harsh_brake", 49.5)])
    assert (m["tp"], m["fp"], m["fn"]) == (2, 0, 0) and m["recall"] == 1.0


def test_wrong_class_is_miss_and_false_positive():
    m = C.match_events(TRUTH, [ev("harsh_brake", 10.1), ev("harsh_brake", 50.5)])
    assert (m["tp"], m["fp"], m["fn"]) == (1, 1, 1)


def test_late_detection_not_matched_and_no_double_use():
    m = C.match_events(TRUTH, [ev("impact", 20.0), ev("impact", 10.1), ev("impact", 10.2)])
    assert m["tp"] == 1 and m["fp"] == 2 and m["fn"] == 1


def test_empty_cases():
    assert C.match_events([], [])["recall"] == 1.0
    assert C.match_events(TRUTH, [])["recall"] == 0.0

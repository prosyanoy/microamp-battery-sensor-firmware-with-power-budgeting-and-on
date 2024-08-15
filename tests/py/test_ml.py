import numpy as np

from ml import features as F
from ml import train as T
from ml.dataset import build


def test_extract_constant_window():
    w = np.tile([-400, 0, 1000], (64, 1))
    f = F.extract(w, [0, 0, 1000])
    assert f[0] == -400 and f[3] == 0 and f[6] == 400 and f[9] == 0 and f[12] == 400 and f[15] == 0


def test_extract_known_alternating_window():
    w = np.zeros((64, 3), dtype=int)
    w[::2, 0] = 100
    w[1::2, 0] = -100
    f = F.extract(w, [0, 0, 0])
    assert f[0] == 0 and f[3] == 100 and f[9] == 200 and f[15] == 200


def test_quantise_saturation_and_rounding():
    mult = [1 << 12] * 16                 # scale 1.0
    assert F.quantise([1000] * 16, mult)[0] == 127
    assert F.quantise([-1000] * 16, mult)[0] == -128
    assert F.quantise([5] * 16, mult)[0] == 5


def test_dataset_has_all_classes():
    X, y = build(range(2), 120.0)
    assert X.shape[1] == 16 and set(np.unique(y)) == {0, 1, 2}


def test_quantised_model_tracks_float_model():
    rng = np.random.default_rng(0)
    X, y = build(range(4), 180.0)
    S = np.maximum(np.percentile(np.abs(X), 99.5, axis=0), 8.0)
    mult = np.rint(127.0 / S * (1 << T.FEAT_SHIFT)).astype(np.int64)
    Xq = np.array([F.quantise(f, mult, T.FEAT_SHIFT) for f in X])
    params = T.fit(Xq / 127.0, y, rng, epochs=8)
    layers = T.quantise_model(params, Xq)
    pf = T.forward(params, Xq / 127.0)[-1].argmax(axis=1)
    pq, logits = T.predict(layers, Xq)
    assert (pf == y).mean() > 0.95
    assert (pq == pf).mean() > 0.97          # int8 agrees with float
    assert logits.min() >= -128 and logits.max() <= 127
    assert layers[0]["w"].dtype == np.int8 and layers[0]["b"].dtype == np.int32


def test_exported_model_header_matches_shipped_metrics():
    import json, pathlib
    root = pathlib.Path(__file__).resolve().parents[2]
    m = json.loads((root / "docs" / "model_metrics.json").read_text())
    assert m["val_acc_int8"] > 0.97
    hdr = (root / "firmware" / "model" / "model_data.h").read_text()
    assert "MODEL_N_IN 16" in hdr and "MODEL_N_CLASSES 3" in hdr

#include "detector_core.h"

#include <string.h>

#include "nn_int8.h"

void detector_init(detector_t *d) { memset(d, 0, sizeof *d); }

void detector_gap(detector_t *d)
{
    d->head = 0;
    d->filled = 0;
    d->since_infer = 0;
}

static void finish_run(detector_t *d, det_event_t *ev)
{
    ev->timestamp_ms = d->run_start_ms;
    ev->cls = d->run_cls;
    ev->peak_mg = d->run_peak;
    ev->confidence = d->run_conf;
    ev->duration_ms = d->run_end_ms - d->run_start_ms;
    d->in_run = false;
    d->run_windows = 0;
}

void detector_push(detector_t *d, const int16_t xyz[3], uint32_t t_ms, det_result_t *res)
{
    memset(res, 0, sizeof *res);
    memcpy(d->ring[d->head], xyz, 3 * sizeof(int16_t));
    d->head = (uint16_t)((d->head + 1) % FEAT_WINDOW);
    if (d->filled < FEAT_WINDOW) d->filled++;
    d->since_infer++;
    if (d->filled < FEAT_WINDOW || d->since_infer < DET_STRIDE) return;
    d->since_infer = 0;

    int16_t win[FEAT_WINDOW][3];              /* linearise ring, oldest first */
    for (int i = 0; i < FEAT_WINDOW; i++)
        memcpy(win[i], d->ring[(d->head + i) % FEAT_WINDOW], 3 * sizeof(int16_t));

    if (!d->have_baseline) {                  /* assume the unit starts at rest */
        int32_t s[3] = {0, 0, 0};
        for (int i = 0; i < FEAT_WINDOW; i++)
            for (int a = 0; a < 3; a++) s[a] += win[i][a];
        for (int a = 0; a < 3; a++) d->baseline[a] = (int16_t)(s[a] / FEAT_WINDOW);
        d->have_baseline = true;
    }

    int32_t feat[FEAT_COUNT];
    int8_t q[FEAT_COUNT];
    nn_result_t nn;
    features_extract((const int16_t(*)[3])win, d->baseline, feat);
    features_quantise(feat, q);
    nn_infer(q, &nn);

    const uint32_t mag_peak = (uint32_t)feat[12];
    const uint32_t t_start = t_ms - (FEAT_WINDOW - 1) * DET_SAMPLE_PERIOD_MS;
    /* Plausibility gate: an impact below DET_IMPACT_MIN_MG cannot be one (a
     * window that only partly overlaps a brake can fool the network after a
     * wake-from-sleep gap). Fall back to the best of the remaining classes. */
    if (nn.cls == CLS_IMPACT && mag_peak < DET_IMPACT_MIN_MG)
        nn.cls = nn.logits[CLS_HARSH_BRAKE] > nn.logits[CLS_NORMAL] ? CLS_HARSH_BRAKE : CLS_NORMAL;
    res->inferred = true;
    res->cls = nn.cls;
    res->macs = nn.macs;
    res->activity = mag_peak > DET_ACTIVITY_MG || nn.cls != CLS_NORMAL;

    if (nn.cls != CLS_NORMAL) {
        if (!d->in_run) {
            d->in_run = true;
            d->run_cls = nn.cls;
            d->run_start_ms = t_start;
            d->run_peak = 0;
            d->run_conf = 0;
            d->run_windows = 0;
        }
        if (nn.cls == CLS_IMPACT) d->run_cls = CLS_IMPACT;   /* impact outranks brake */
        if (mag_peak > d->run_peak) d->run_peak = mag_peak;
        if (nn.margin > d->run_conf) d->run_conf = nn.margin;
        d->run_end_ms = t_ms;
        d->run_windows++;
        if (d->run_windows >= DET_MAX_RUN_WINDOWS) {
            finish_run(d, &res->event);
            res->event_ready = true;
        }
    } else {
        if (d->in_run) {
            finish_run(d, &res->event);
            res->event_ready = true;
        }
        if (mag_peak < 100) {                 /* quiet: track slow gravity drift */
            for (int a = 0; a < 3; a++) d->baseline[a] = (int16_t)(d->baseline[a] + (feat[a] >> 3));
        }
    }
}

bool detector_flush(detector_t *d, det_event_t *ev)
{
    if (!d->in_run) return false;
    finish_run(d, ev);
    return true;
}

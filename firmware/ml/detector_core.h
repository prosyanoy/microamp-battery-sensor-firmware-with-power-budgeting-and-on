#ifndef DETECTOR_CORE_H
#define DETECTOR_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "ml_features.h"

#define DET_STRIDE 32          /* inference every 32 samples (0.32 s at 100 Hz) */
#define DET_SAMPLE_PERIOD_MS 10
#define DET_MAX_RUN_WINDOWS 8
#define DET_IMPACT_MIN_MG 2000 /* smaller peaks are never reported as impacts */
#define DET_ACTIVITY_MG 150    /* |a - g| above this counts as "motion" */

enum { CLS_NORMAL = 0, CLS_HARSH_BRAKE = 1, CLS_IMPACT = 2 };

typedef struct {
    uint32_t timestamp_ms;
    uint8_t cls;
    uint32_t peak_mg;
    uint8_t confidence;
    uint32_t duration_ms;
} det_event_t;

typedef struct {
    bool inferred;          /* a window was classified on this sample */
    bool activity;          /* window showed motion (drives the sleep policy) */
    bool event_ready;       /* `event` holds a finished event */
    uint8_t cls;
    uint32_t macs;
    det_event_t event;
} det_result_t;

typedef struct {
    int16_t ring[FEAT_WINDOW][3];
    uint16_t head;          /* next write slot */
    uint16_t filled;
    uint16_t since_infer;
    bool have_baseline;
    int16_t baseline[3];
    /* current event run */
    bool in_run;
    uint8_t run_cls, run_windows;
    uint32_t run_start_ms, run_peak, run_end_ms;
    uint8_t run_conf;
} detector_t;

void detector_init(detector_t *d);
/* Sample stream discontinuity (e.g. after deep sleep): drop partial window. */
void detector_gap(detector_t *d);
/* Feed one sample taken at `t_ms`. */
void detector_push(detector_t *d, const int16_t xyz[3], uint32_t t_ms, det_result_t *res);
/* Flush an in-flight event run (end of recording). */
bool detector_flush(detector_t *d, det_event_t *ev);

#endif

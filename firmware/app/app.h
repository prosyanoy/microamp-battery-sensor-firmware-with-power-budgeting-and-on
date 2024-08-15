#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stdint.h>

#include "detector_core.h"

/* A firmware "design" = one point in the power/latency trade-off space. */
typedef struct {
    const char *name;
    bool tickless;       /* deep sleep between wakeups (no 1 kHz tick while idle) */
    bool use_fifo;       /* batch samples in the sensor FIFO, wake on watermark   */
    bool motion_gate;    /* sleep through quiet periods, wake on motion interrupt */
    uint16_t odr_hz;
    uint8_t watermark;
} design_t;

typedef struct {
    const design_t *design;
    uint32_t duration_ms;
    uint32_t status_period_ms;
    const char *log_path;
    const char *report_path;
    const char *python;
    const char *sim_dir;
    const char *scenario;
    const char *truth_path;
    uint32_t seed;
    uint32_t battery_mah;
} app_config_t;

typedef struct {
    uint32_t samples, windows, events, wakes_irq, fifo_overruns, queue_drops, motion_wakes;
    uint32_t i2c_xfers, i2c_bytes, radio_packets, radio_bytes;
    uint64_t macs;
} app_stats_t;

#define SAMPLE_BATCH_MAX 32
typedef struct {
    uint32_t t_last_ms;
    uint8_t n;
    bool gap;
    int16_t mg[SAMPLE_BATCH_MAX][3];
} sample_batch_t;

extern app_config_t g_cfg;
extern app_stats_t g_stats;
extern volatile uint32_t g_last_activity_ms;

const design_t *design_by_name(const char *name);
const design_t *design_at(int i);
int design_count(void);

void app_start_tasks(void);
void app_irq_isr(void);            /* gives the sampler's wake semaphore */
bool app_sensor_healthy(void);
void stat_add(uint32_t *counter, uint32_t v);

#endif

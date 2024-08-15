/* Sampler: owns the accelerometer. Three behaviours, selected by the design:
 *   polling : vTaskDelayUntil at the sample rate, read one sample per period.
 *   fifo    : sleep on the IRQ semaphore, drain the FIFO on watermark.
 *   wom     : as fifo, but while the detector sees no activity for QUIET_HOLD_MS
 *             the INT pin is re-routed to the motion detector so the MCU
 *             sleeps until something moves (the FIFO keeps 0.32 s of pre-roll). */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "accel_driver.h"
#include "energy.h"
#include "hal.h"
#include "rtos.h"

#define QUIET_HOLD_MS 3000
#define MOTION_THRESHOLD 1          /* 1 LSB = 186 mg at +-16 g, on the high-passed signal */

static uint32_t now_ms(void) { return (uint32_t)xTaskGetTickCount(); }

void app_irq_isr(void)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(g_irq_sem, &woken);
    portYIELD_FROM_ISR(woken);
}

static void push_batch(const int16_t (*mg)[3], uint8_t n, bool gap)
{
    static sample_batch_t b;           /* static: keeps the task stack small */
    b.t_last_ms = now_ms();
    b.n = n;
    b.gap = gap;
    memcpy(b.mg, mg, (size_t)n * sizeof b.mg[0]);
    energy_charge_cycles(PS_SAMPLE, 300u + 40u * n);           /* ISR + unpack + copy */
    stat_add(&g_stats.samples, n);
    if (xQueueSend(g_batch_q, &b, 0) != pdTRUE) stat_add(&g_stats.queue_drops, 1);
}

static void fatal(const char *what, int rc)
{
    fprintf(stderr, "microamp: sensor %s failed (%d)\n", what, rc);
    _exit(3);
}

static void run_polling(const design_t *d)
{
    accel_cfg_t cfg = {d->odr_hz, ACCEL_FIFO_BYPASS, 1};
    if (accel_configure(&cfg) < 0) fatal("configure", -1);
    energy_set_sensor_odr(d->odr_hz, hal_now_us());
    TickType_t last = xTaskGetTickCount();
    const TickType_t period = 1000 / d->odr_hz;
    int16_t s[1][3];
    for (;;) {
        vTaskDelayUntil(&last, period);
        bool ready;
        xSemaphoreTake(g_bus_mutex, portMAX_DELAY);
        int rc = accel_status_drdy(&ready);
        if (rc == 0 && ready) rc = accel_read_sample(s[0]);
        xSemaphoreGive(g_bus_mutex);
        if (rc == 0 && ready) push_batch((const int16_t(*)[3])s, 1, false);
    }
}

static void drain_fifo(bool gap)
{
    int16_t buf[ACCEL_FIFO_DEPTH][3];
    uint8_t n, level;
    bool ovr;
    xSemaphoreTake(g_bus_mutex, portMAX_DELAY);
    do {
        if (accel_fifo_level(&level, &ovr) < 0) break;
        if (ovr) stat_add(&g_stats.fifo_overruns, 1);
        if (accel_fifo_read(buf, ACCEL_FIFO_DEPTH, &n) < 0 || n == 0) break;
        push_batch((const int16_t(*)[3])buf, n, gap);
        gap = false;
    } while (hal_irq_level());
    xSemaphoreGive(g_bus_mutex);
}

static void run_fifo(const design_t *d)
{
    accel_cfg_t cfg = {d->odr_hz, ACCEL_FIFO_STREAM, d->watermark};
    xSemaphoreTake(g_bus_mutex, portMAX_DELAY);
    int rc = accel_configure(&cfg);
    if (rc == 0 && d->motion_gate) rc = accel_enable_motion(MOTION_THRESHOLD);
    bool quiet = d->motion_gate;
    if (rc == 0) rc = accel_route_irq(quiet ? ACCEL_IRQ_MOTION : ACCEL_IRQ_FIFO_WATERMARK);
    xSemaphoreGive(g_bus_mutex);
    if (rc < 0) fatal("configure", rc);
    energy_set_sensor_odr(d->odr_hz, hal_now_us());
    g_last_activity_ms = now_ms();

    for (;;) {
        xSemaphoreTake(g_irq_sem, portMAX_DELAY);
        stat_add(&g_stats.wakes_irq, 1);
        bool gap = false;
        xSemaphoreTake(g_bus_mutex, portMAX_DELAY);
        if (quiet) {                                   /* motion wake: switch to streaming */
            accel_clear_motion();
            accel_route_irq(ACCEL_IRQ_FIFO_WATERMARK);
            quiet = false;
            gap = true;
            g_last_activity_ms = now_ms();
            stat_add(&g_stats.motion_wakes, 1);
        }
        xSemaphoreGive(g_bus_mutex);
        drain_fifo(gap);
        xSemaphoreTake(g_irq_sem, 0);                  /* drop wakeups we just served */

        if (d->motion_gate && !quiet && now_ms() - g_last_activity_ms >= QUIET_HOLD_MS) {
            xSemaphoreTake(g_bus_mutex, portMAX_DELAY);
            accel_route_irq(ACCEL_IRQ_MOTION);
            accel_clear_motion();
            xSemaphoreGive(g_bus_mutex);
            quiet = true;
            xSemaphoreTake(g_irq_sem, 0);
        }
    }
}

void sampler_task(void *arg)
{
    const design_t *d = arg;
    xSemaphoreTake(g_bus_mutex, portMAX_DELAY);
    int rc = accel_init();
    xSemaphoreGive(g_bus_mutex);
    if (rc < 0) fatal("init", rc);
    hal_irq_set_handler(app_irq_isr);
    if (d->use_fifo) run_fifo(d); else run_polling(d);
}

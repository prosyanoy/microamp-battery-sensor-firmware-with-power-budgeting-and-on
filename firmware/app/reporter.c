/* Reporter: serialises events (and a periodic status heartbeat with the
 * battery-life estimate) with nanopb and "transmits" them through the HAL. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <pb_encode.h>

#include "accel_driver.h"
#include "energy.h"
#include "hal.h"
#include "hal_posix_sim.h"
#include "microamp.pb.h"
#include "rtos.h"

static uint32_t seq;

static void transmit(const LogRecord *rec)
{
    uint8_t buf[LogRecord_size];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof buf);
    if (!pb_encode(&os, LogRecord_fields, rec)) return;
    hal_radio_send(buf, os.bytes_written);
}

static void send_event(const det_event_t *e)
{
    LogRecord r = LogRecord_init_zero;
    r.seq = seq++;
    r.which_payload = LogRecord_event_tag;
    r.payload.event.timestamp_ms = e->timestamp_ms;
    r.payload.event.event_class = (EventClass)e->cls;
    r.payload.event.peak_mg = e->peak_mg;
    r.payload.event.confidence = e->confidence;
    r.payload.event.duration_ms = e->duration_ms;
    stat_add(&g_stats.events, 1);
    transmit(&r);
}

static void send_status(void)
{
    energy_report_t er;
    energy_report(hal_now_us(), &er);
    LogRecord r = LogRecord_init_zero;
    r.seq = seq++;
    r.which_payload = LogRecord_status_tag;
    r.payload.status.timestamp_ms = (uint32_t)(hal_now_us() / 1000u);
    r.payload.status.battery_years_x100 = er.battery_years_x100;
    r.payload.status.avg_current_na = er.avg_current_na;
    r.payload.status.events_total = g_stats.events;
    r.payload.status.windows_total = g_stats.windows;
    r.payload.status.fifo_overruns = g_stats.fifo_overruns;
    r.payload.status.sensor_ok = app_sensor_healthy();
    transmit(&r);
}

static void write_report(void)
{
    if (!g_cfg.report_path) return;
    FILE *f = fopen(g_cfg.report_path, "w");
    if (!f) return;
    energy_report_t er;
    energy_report(hal_now_us(), &er);
    fprintf(f, "{\n  \"design\": \"%s\",\n  \"scenario\": \"%s\",\n  \"duration_s\": %.3f,\n",
            g_cfg.design->name, g_cfg.scenario, (double)er.elapsed_us / 1e6);
    fprintf(f, "  \"avg_current_na\": %u,\n  \"battery_years\": %.2f,\n  \"battery_mah\": %u,\n",
            er.avg_current_na, er.battery_years_x100 / 100.0, g_cfg.battery_mah);
    fprintf(f, "  \"states\": {\n");
    for (int s = 0; s < PS_COUNT; s++)
        fprintf(f, "    \"%s\": {\"time_us\": %llu, \"charge_nc\": %.1f, \"share_pct\": %.2f}%s\n",
                pstate_names[s], (unsigned long long)er.time_us[s], er.charge_fc[s] / 1e6,
                er.total_charge_fc ? 100.0 * er.charge_fc[s] / er.total_charge_fc : 0.0,
                s + 1 < PS_COUNT ? "," : "");
    fprintf(f, "  },\n  \"sensor_charge_nc\": %.1f,\n  \"sensor_share_pct\": %.2f,\n",
            er.sensor_charge_fc / 1e6,
            er.total_charge_fc ? 100.0 * er.sensor_charge_fc / er.total_charge_fc : 0.0);
    fprintf(f, "  \"wakes\": %u,\n  \"stats\": {\"samples\": %u, \"windows\": %u, \"events\": %u, "
               "\"irq_wakes\": %u, \"motion_wakes\": %u, \"fifo_overruns\": %u, \"queue_drops\": %u, "
               "\"i2c_xfers\": %u, \"i2c_bytes\": %u, \"radio_packets\": %u, \"radio_bytes\": %u, "
               "\"macs\": %llu}\n}\n",
            er.wakes, g_stats.samples, g_stats.windows, g_stats.events, g_stats.wakes_irq,
            g_stats.motion_wakes, g_stats.fifo_overruns, g_stats.queue_drops, g_stats.i2c_xfers,
            g_stats.i2c_bytes, g_stats.radio_packets, g_stats.radio_bytes,
            (unsigned long long)g_stats.macs);
    fclose(f);
}

bool app_sensor_healthy(void)
{
    uint8_t id = 0;
    xSemaphoreTake(g_bus_mutex, portMAX_DELAY);
    int rc = accel_who_am_i(&id);
    xSemaphoreGive(g_bus_mutex);
    return rc == 0 && id == 0x33;
}

void reporter_task(void *arg)
{
    (void)arg;
    det_event_t ev;
    uint32_t next_status = g_cfg.status_period_ms;

    for (;;) {
        uint32_t now = (uint32_t)xTaskGetTickCount();
        uint32_t deadline = next_status < g_cfg.duration_ms ? next_status : g_cfg.duration_ms;
        TickType_t wait = deadline > now ? deadline - now : 0;
        if (xQueueReceive(g_event_q, &ev, wait) == pdTRUE) {
            send_event(&ev);
            continue;
        }
        now = (uint32_t)xTaskGetTickCount();
        if (now >= g_cfg.duration_ms) break;
        send_status();
        next_status += g_cfg.status_period_ms;
    }
    while (xQueueReceive(g_event_q, &ev, 0) == pdTRUE) send_event(&ev);
    send_status();
    write_report();
    hal_posix_stop();
    fflush(NULL);
    _exit(0);
}

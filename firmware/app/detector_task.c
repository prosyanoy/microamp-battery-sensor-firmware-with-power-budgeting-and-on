#include "energy.h"
#include "rtos.h"

void detector_task(void *arg)
{
    (void)arg;
    static detector_t det;
    static sample_batch_t b;
    detector_init(&det);
    const energy_params_t *ep = energy_params();

    for (;;) {
        xQueueReceive(g_batch_q, &b, portMAX_DELAY);
        if (b.gap) detector_gap(&det);
        for (uint8_t i = 0; i < b.n; i++) {
            det_result_t r;
            uint32_t t = b.t_last_ms - (uint32_t)(b.n - 1 - i) * DET_SAMPLE_PERIOD_MS;
            detector_push(&det, b.mg[i], t, &r);
            if (!r.inferred) continue;
            /* Cost model of the window: feature pass over 64 samples + MACs. */
            energy_charge_cycles(PS_INFER, FEAT_WINDOW * ep->cyc_per_feat_sample + r.macs * ep->cyc_per_mac);
            stat_add(&g_stats.windows, 1);
            g_stats.macs += r.macs;
            if (r.activity) g_last_activity_ms = t;
            if (r.event_ready && xQueueSend(g_event_q, &r.event, 0) != pdTRUE)
                stat_add(&g_stats.queue_drops, 1);
        }
    }
}

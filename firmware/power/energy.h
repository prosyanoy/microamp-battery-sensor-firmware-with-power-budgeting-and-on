/* Energy ledger and battery-life model.
 *
 * The firmware tells the ledger how long the CPU/radio were busy in each
 * state; everything else in the elapsed time is billed as sleep (tickless
 * designs) or idle-WFI (tick-driven designs). Currents are in nA, charge in
 * nA*us (= femtocoulombs). All integer math. */
#ifndef ENERGY_H
#define ENERGY_H

#include <stdint.h>

typedef enum { PS_SLEEP, PS_IDLE, PS_WAKE, PS_SAMPLE, PS_INFER, PS_RADIO, PS_COUNT } pstate_t;

typedef struct {
    uint32_t current_na[PS_COUNT];
    uint32_t cpu_hz;
    uint32_t wake_us;          /* wake-from-stop + scheduler cost per wake    */
    uint32_t tick_us;          /* tick ISR cost when the tick runs (non-tickless) */
    uint32_t cyc_per_mac;      /* Cortex-M4 class estimates                    */
    uint32_t cyc_per_feat_sample;
    uint32_t i2c_hz;
    uint32_t radio_startup_us;
    uint32_t radio_overhead_bytes;
    uint32_t battery_mah;
    uint32_t usable_pct;
    uint32_t self_discharge_pct_x100;  /* % per year * 100 */
} energy_params_t;

typedef struct {
    uint64_t elapsed_us;
    uint64_t time_us[PS_COUNT];
    uint64_t charge_fc[PS_COUNT];     /* nA*us */
    uint64_t sensor_charge_fc;
    uint64_t total_charge_fc;
    uint32_t avg_current_na;
    uint32_t battery_years_x100;
    uint32_t wakes;
} energy_report_t;

extern const char *const pstate_names[PS_COUNT];

void energy_default_params(energy_params_t *p);
void energy_init(const energy_params_t *p, int tickless);
const energy_params_t *energy_params(void);

/* Ledger updates come from tasks and from the idle hook, so the application
 * installs a critical-section lock (a mutex cannot be taken from idle). */
void energy_set_lock_hooks(void (*lock)(void), void (*unlock)(void));

void energy_charge_us(pstate_t st, uint32_t us);
void energy_charge_cycles(pstate_t st, uint32_t cycles);
void energy_note_wake(void);
void energy_set_sensor_odr(uint32_t odr_hz, uint64_t now_us);   /* 0 = power-down */

uint32_t energy_sensor_current_na(uint32_t odr_hz);
void energy_report(uint64_t now_us, energy_report_t *out);

/* Pure helper (unit tested): battery life in years*100 for an average draw. */
uint32_t energy_battery_years_x100(const energy_params_t *p, uint32_t avg_na);

#endif

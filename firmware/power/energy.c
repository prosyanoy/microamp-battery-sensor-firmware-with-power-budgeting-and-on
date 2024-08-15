#include "energy.h"

#include <string.h>


const char *const pstate_names[PS_COUNT] = {"sleep", "idle", "wake", "sample", "inference", "radio"};

static void (*lock_fn)(void), (*unlock_fn)(void);
#define ENERGY_LOCK() do { if (lock_fn) lock_fn(); } while (0)
#define ENERGY_UNLOCK() do { if (unlock_fn) unlock_fn(); } while (0)

void energy_set_lock_hooks(void (*lock)(void), void (*unlock)(void))
{
    lock_fn = lock;
    unlock_fn = unlock;
}

static energy_params_t P;
static int tickless_mode;
static uint64_t busy_us[PS_COUNT];
static uint32_t wake_count;
static uint32_t sensor_odr;
static uint64_t sensor_since_us;
static uint64_t sensor_charge;

void energy_default_params(energy_params_t *p)
{
    /* Illustrative numbers in the range of an STM32L4-class MCU at 16 MHz, a
     * BLE radio burst and a LIS3DH-class accelerometer. */
    p->current_na[PS_SLEEP] = 2000;       /* STOP2 + RTC + RAM retention      */
    p->current_na[PS_IDLE] = 450000;      /* Sleep/WFI with clocks running    */
    p->current_na[PS_WAKE] = 1600000;     /* run mode                         */
    p->current_na[PS_SAMPLE] = 900000;      /* CPU in WFI while I2C DMA runs + pull-ups */
    p->current_na[PS_INFER] = 1600000;
    p->current_na[PS_RADIO] = 8000000;    /* radio TX + CPU                   */
    p->cpu_hz = 16000000;
    p->wake_us = 40;
    p->tick_us = 6;
    p->cyc_per_mac = 3;
    p->cyc_per_feat_sample = 250;
    p->i2c_hz = 400000;
    p->radio_startup_us = 1500;
    p->radio_overhead_bytes = 14;
    p->battery_mah = 225;                 /* CR2032 */
    p->usable_pct = 85;
    p->self_discharge_pct_x100 = 100;     /* 1 %/year */
}

void energy_init(const energy_params_t *p, int tickless)
{
    P = *p;
    tickless_mode = tickless;
    memset(busy_us, 0, sizeof busy_us);
    wake_count = 0;
    sensor_odr = 0;
    sensor_since_us = 0;
    sensor_charge = 0;
}

const energy_params_t *energy_params(void) { return &P; }

void energy_charge_us(pstate_t st, uint32_t us)
{
    ENERGY_LOCK();
    busy_us[st] += us;
    ENERGY_UNLOCK();
}

void energy_charge_cycles(pstate_t st, uint32_t cycles)
{
    uint64_t us = ((uint64_t)cycles * 1000000u + P.cpu_hz - 1) / P.cpu_hz;
    energy_charge_us(st, (uint32_t)us);
}

void energy_note_wake(void)
{
    ENERGY_LOCK();
    wake_count++;
    ENERGY_UNLOCK();
}

uint32_t energy_sensor_current_na(uint32_t odr_hz)
{
    /* High-resolution mode, 2.5 V; datasheet-like orders of magnitude. */
    if (odr_hz == 0) return 500;
    if (odr_hz <= 1) return 3000;
    if (odr_hz <= 10) return 4000;
    if (odr_hz <= 25) return 6000;
    if (odr_hz <= 50) return 11000;
    if (odr_hz <= 100) return 20000;
    if (odr_hz <= 200) return 30000;
    return 50000;
}

void energy_set_sensor_odr(uint32_t odr_hz, uint64_t now_us)
{
    ENERGY_LOCK();
    if (now_us > sensor_since_us)
        sensor_charge += (now_us - sensor_since_us) * energy_sensor_current_na(sensor_odr);
    sensor_since_us = now_us > sensor_since_us ? now_us : sensor_since_us;
    sensor_odr = odr_hz;
    ENERGY_UNLOCK();
}

uint32_t energy_battery_years_x100(const energy_params_t *p, uint32_t avg_na)
{
    uint64_t usable_nah = (uint64_t)p->battery_mah * 1000000ull * p->usable_pct / 100u;
    uint64_t sd_na = (uint64_t)p->battery_mah * 1000000ull * p->self_discharge_pct_x100 / 10000u / 8766u;
    uint64_t draw = (uint64_t)avg_na + sd_na;
    if (draw == 0) return 0xFFFFFFFFu;
    uint64_t y = usable_nah * 100u / (draw * 8766u);
    return y > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)y;
}

void energy_report(uint64_t now_us, energy_report_t *r)
{
    uint64_t busy[PS_COUNT];
    uint32_t wakes;
    uint64_t s_charge;

    ENERGY_LOCK();
    memcpy(busy, busy_us, sizeof busy);
    wakes = wake_count;
    s_charge = sensor_charge;
    if (now_us > sensor_since_us)
        s_charge += (now_us - sensor_since_us) * energy_sensor_current_na(sensor_odr);
    ENERGY_UNLOCK();

    memset(r, 0, sizeof *r);
    r->elapsed_us = now_us;
    r->wakes = wakes;
    busy[PS_WAKE] += (uint64_t)wakes * P.wake_us;
    if (!tickless_mode) busy[PS_WAKE] += (now_us / 1000u) * P.tick_us;   /* 1 kHz tick ISR */

    uint64_t busy_total = 0;
    for (int s = PS_WAKE; s < PS_COUNT; s++) busy_total += busy[s];
    if (busy_total > now_us) {         /* never bill more than wall time */
        for (int s = PS_WAKE; s < PS_COUNT; s++) busy[s] = busy[s] * now_us / busy_total;
        busy_total = now_us;
    }
    uint64_t quiet = now_us - busy_total;
    busy[tickless_mode ? PS_SLEEP : PS_IDLE] = quiet;
    busy[tickless_mode ? PS_IDLE : PS_SLEEP] = 0;

    for (int s = 0; s < PS_COUNT; s++) {
        r->time_us[s] = busy[s];
        r->charge_fc[s] = busy[s] * P.current_na[s];
        r->total_charge_fc += r->charge_fc[s];
    }
    r->sensor_charge_fc = s_charge;
    r->total_charge_fc += s_charge;
    uint64_t ms = now_us / 1000u;
    r->avg_current_na = ms ? (uint32_t)(r->total_charge_fc / 1000u / ms) : 0;
    r->battery_years_x100 = energy_battery_years_x100(&P, r->avg_current_na);
}

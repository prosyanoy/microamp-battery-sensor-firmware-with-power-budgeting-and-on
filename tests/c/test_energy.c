#include "energy.h"
#include "unity.h"

static energy_params_t P;
void setUp(void) { energy_default_params(&P); energy_init(&P, 1); }
void tearDown(void) {}

static void test_pure_sleep_battery_life(void)
{
    energy_report_t r;
    energy_report(1000ull * 1000000ull, &r);     /* 1000 s, nothing happened */
    TEST_ASSERT_EQUAL_UINT32(2500, r.avg_current_na);   /* 2 uA MCU stop + 0.5 uA sensor power-down */
}

static void test_years_formula(void)
{
    /* 225 mAh * 85 % = 191.25 mAh; 1 mA -> 191.25 h minus self-discharge. */
    uint32_t y = energy_battery_years_x100(&P, 10000);   /* 10 uA */
    TEST_ASSERT_UINT32_WITHIN(2, 212, y);                /* 191.25 mAh / (10 uA + 0.26 uA self-discharge) */
    TEST_ASSERT_TRUE(energy_battery_years_x100(&P, 1000) > energy_battery_years_x100(&P, 2000));
}

static void test_busy_time_is_billed_at_state_current(void)
{
    energy_charge_us(PS_RADIO, 1000);
    energy_report_t r;
    energy_report(1000000, &r);                    /* 1 s */
    TEST_ASSERT_EQUAL_UINT64(1000, r.time_us[PS_RADIO]);
    TEST_ASSERT_EQUAL_UINT64(1000ull * P.current_na[PS_RADIO], r.charge_fc[PS_RADIO]);
    TEST_ASSERT_EQUAL_UINT64(1000000 - 1000, r.time_us[PS_SLEEP]);
}

static void test_state_times_sum_to_elapsed(void)
{
    energy_charge_us(PS_SAMPLE, 5000);
    energy_charge_cycles(PS_INFER, 16000);         /* 1 ms at 16 MHz */
    energy_note_wake();
    energy_note_wake();
    energy_report_t r;
    energy_report(2000000, &r);
    uint64_t sum = 0;
    for (int s = 0; s < PS_COUNT; s++) sum += r.time_us[s];
    TEST_ASSERT_EQUAL_UINT64(2000000, sum);
    TEST_ASSERT_EQUAL_UINT64(1000, r.time_us[PS_INFER]);
    TEST_ASSERT_EQUAL_UINT64(2u * P.wake_us, r.time_us[PS_WAKE]);
}

static void test_tick_driven_design_idles_and_pays_for_ticks(void)
{
    energy_init(&P, 0);
    energy_report_t r;
    energy_report(1000000, &r);
    TEST_ASSERT_EQUAL_UINT64(0, r.time_us[PS_SLEEP]);
    TEST_ASSERT_EQUAL_UINT64(1000u * P.tick_us, r.time_us[PS_WAKE]);
    TEST_ASSERT_TRUE(r.avg_current_na > 400000);
}

static void test_sensor_charge_follows_odr_changes(void)
{
    energy_set_sensor_odr(100, 0);
    energy_set_sensor_odr(0, 1000000);             /* 1 s at 100 Hz */
    energy_report_t r;
    energy_report(3000000, &r);                    /* 2 more s powered down */
    uint64_t expect = 1000000ull * energy_sensor_current_na(100) + 2000000ull * energy_sensor_current_na(0);
    TEST_ASSERT_EQUAL_UINT64(expect, r.sensor_charge_fc);
}

static void test_never_bills_more_than_wall_time(void)
{
    energy_charge_us(PS_RADIO, 5000000);
    energy_report_t r;
    energy_report(1000000, &r);
    uint64_t sum = 0;
    for (int s = 0; s < PS_COUNT; s++) sum += r.time_us[s];
    TEST_ASSERT_EQUAL_UINT64(1000000, sum);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pure_sleep_battery_life);
    RUN_TEST(test_years_formula);
    RUN_TEST(test_busy_time_is_billed_at_state_current);
    RUN_TEST(test_state_times_sum_to_elapsed);
    RUN_TEST(test_tick_driven_design_idles_and_pays_for_ticks);
    RUN_TEST(test_sensor_charge_follows_odr_changes);
    RUN_TEST(test_never_bills_more_than_wall_time);
    return UNITY_END();
}

#include <string.h>

#include "detector_core.h"
#include "unity.h"

static detector_t d;
static det_event_t events[16];
static int n_events;
static uint32_t t;
static int windows;

void setUp(void) { detector_init(&d); n_events = 0; t = 0; windows = 0; }
void tearDown(void) {}

static void feed(int16_t x, int16_t y, int16_t z)
{
    int16_t s[3] = {x, y, z};
    det_result_t r;
    t += DET_SAMPLE_PERIOD_MS;
    detector_push(&d, s, t, &r);
    windows += r.inferred;
    if (r.event_ready && n_events < 16) events[n_events++] = r.event;
}

static void rest(int n) { for (int i = 0; i < n; i++) feed((int16_t)((i % 3) - 1), 0, 1000); }

static void test_no_inference_until_window_full_then_every_stride(void)
{
    rest(FEAT_WINDOW - 1);
    TEST_ASSERT_EQUAL(0, windows);
    rest(1);
    TEST_ASSERT_EQUAL(1, windows);
    rest(DET_STRIDE * 3);
    TEST_ASSERT_EQUAL(4, windows);
}

static void test_quiet_stream_yields_no_events(void)
{
    rest(2000);
    TEST_ASSERT_EQUAL(0, n_events);
}

static void test_sustained_deceleration_is_harsh_brake(void)
{
    rest(300);
    for (int i = 0; i < 110; i++) feed(-750, 0, 1100);
    rest(300);
    TEST_ASSERT_EQUAL(1, n_events);
    TEST_ASSERT_EQUAL_UINT8(CLS_HARSH_BRAKE, events[0].cls);
    TEST_ASSERT_TRUE(events[0].peak_mg > 500);
    TEST_ASSERT_TRUE(events[0].duration_ms > 0);
}

static void test_spike_is_impact(void)
{
    rest(300);
    for (int i = 0; i < 20; i++) {
        int16_t a = (int16_t)(9000 >> (i / 3));
        feed((i & 1) ? a : (int16_t)-a, (int16_t)(a / 2), (int16_t)(1000 + ((i & 2) ? a / 3 : -a / 3)));
    }
    rest(300);
    TEST_ASSERT_TRUE(n_events >= 1);
    TEST_ASSERT_EQUAL_UINT8(CLS_IMPACT, events[0].cls);
    TEST_ASSERT_TRUE(events[0].peak_mg > 2000);
}

static void test_gap_discards_partial_window(void)
{
    rest(40);
    detector_gap(&d);
    rest(40);
    TEST_ASSERT_EQUAL(0, windows);
    rest(30);
    TEST_ASSERT_EQUAL(1, windows);
}

static void test_flush_emits_open_event(void)
{
    rest(300);
    for (int i = 0; i < 100; i++) feed(-750, 0, 1100);
    det_event_t ev;
    TEST_ASSERT_TRUE(detector_flush(&d, &ev));
    TEST_ASSERT_FALSE(detector_flush(&d, &ev));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_inference_until_window_full_then_every_stride);
    RUN_TEST(test_quiet_stream_yields_no_events);
    RUN_TEST(test_sustained_deceleration_is_harsh_brake);
    RUN_TEST(test_spike_is_impact);
    RUN_TEST(test_gap_discards_partial_window);
    RUN_TEST(test_flush_emits_open_event);
    return UNITY_END();
}

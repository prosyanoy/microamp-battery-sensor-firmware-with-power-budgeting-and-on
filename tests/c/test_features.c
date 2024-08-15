#include <string.h>

#include "ml_features.h"
#include "golden_vectors.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_isqrt_exact(void)
{
    for (uint64_t v = 0; v < 5000; v++) {
        uint64_t r = isqrt64(v);
        TEST_ASSERT_TRUE(r * r <= v && (r + 1) * (r + 1) > v);
    }
    TEST_ASSERT_EQUAL_UINT32(4294967295u, isqrt64(0xFFFFFFFE00000001ull));
    TEST_ASSERT_EQUAL_UINT32(46340, isqrt64(2147395600ull));
}

static void test_constant_window_has_zero_spread(void)
{
    int16_t win[FEAT_WINDOW][3];
    const int16_t base[3] = {0, 0, 1000};
    for (int i = 0; i < FEAT_WINDOW; i++) { win[i][0] = -400; win[i][1] = 0; win[i][2] = 1000; }
    int32_t f[FEAT_COUNT];
    features_extract((const int16_t(*)[3])win, base, f);
    TEST_ASSERT_EQUAL_INT32(-400, f[0]);
    TEST_ASSERT_EQUAL_INT32(0, f[3]);              /* std */
    TEST_ASSERT_EQUAL_INT32(400, f[6]);            /* max abs */
    TEST_ASSERT_EQUAL_INT32(0, f[9]);              /* jerk */
    TEST_ASSERT_EQUAL_INT32(400, f[12]);           /* magnitude peak */
    TEST_ASSERT_EQUAL_INT32(0, f[15]);             /* peak-to-peak */
}

static void test_golden_features_match_python(void)
{
    for (int g = 0; g < GOLDEN_N; g++) {
        int32_t f[FEAT_COUNT];
        features_extract((const int16_t(*)[3])golden_win[g], golden_baseline, f);
        TEST_ASSERT_EQUAL_INT32_ARRAY(golden_feat[g], f, FEAT_COUNT);
    }
}

static void test_quantise_saturates(void)
{
    int32_t f[FEAT_COUNT];
    int8_t q[FEAT_COUNT];
    for (int i = 0; i < FEAT_COUNT; i++) f[i] = 1000000;
    features_quantise(f, q);
    for (int i = 0; i < FEAT_COUNT; i++) TEST_ASSERT_EQUAL_INT8(127, q[i]);
    for (int i = 0; i < FEAT_COUNT; i++) f[i] = -1000000;
    features_quantise(f, q);
    for (int i = 0; i < FEAT_COUNT; i++) TEST_ASSERT_EQUAL_INT8(-128, q[i]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_isqrt_exact);
    RUN_TEST(test_constant_window_has_zero_spread);
    RUN_TEST(test_golden_features_match_python);
    RUN_TEST(test_quantise_saturates);
    return UNITY_END();
}

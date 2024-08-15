#include "ml_features.h"
#include "golden_vectors.h"
#include "nn_int8.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_golden_logits_bit_exact_vs_numpy_reference(void)
{
    for (int g = 0; g < GOLDEN_N; g++) {
        int32_t f[FEAT_COUNT];
        int8_t q[FEAT_COUNT];
        nn_result_t r;
        features_extract((const int16_t(*)[3])golden_win[g], golden_baseline, f);
        features_quantise(f, q);
        nn_infer(q, &r);
        for (int c = 0; c < MODEL_N_CLASSES; c++) TEST_ASSERT_EQUAL_INT32(golden_logits[g][c], r.logits[c]);
        TEST_ASSERT_EQUAL_UINT8(golden_class[g], r.cls);
    }
}

static void test_mac_count_matches_architecture(void)
{
    int8_t q[FEAT_COUNT] = {0};
    nn_result_t r;
    nn_infer(q, &r);
    uint32_t expect = 0;
    for (int l = 0; l < MODEL_N_LAYERS; l++) expect += (uint32_t)model_dims[l] * model_dims[l + 1];
    TEST_ASSERT_EQUAL_UINT32(expect, r.macs);
}

static void test_margin_is_top_minus_second(void)
{
    int8_t q[FEAT_COUNT] = {0};
    nn_result_t r;
    nn_infer(q, &r);
    int8_t top = r.logits[r.cls], second = -128;
    for (int c = 0; c < MODEL_N_CLASSES; c++)
        if (c != r.cls && r.logits[c] > second) second = r.logits[c];
    TEST_ASSERT_EQUAL_UINT8(top - second, r.margin);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_golden_logits_bit_exact_vs_numpy_reference);
    RUN_TEST(test_mac_count_matches_architecture);
    RUN_TEST(test_margin_is_top_minus_second);
    return UNITY_END();
}

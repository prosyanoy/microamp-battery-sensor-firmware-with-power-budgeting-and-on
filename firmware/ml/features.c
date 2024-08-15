#include "ml_features.h"

#include "model_data.h"

uint32_t isqrt64(uint64_t v)
{
    uint64_t res = 0, bit = 1ull << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= res + bit) {
            v -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)res;
}

void features_extract(const int16_t win[FEAT_WINDOW][3], const int16_t baseline[3],
                      int32_t out[FEAT_COUNT])
{
    int64_t sum[3] = {0, 0, 0}, sumsq[3] = {0, 0, 0}, jerk[3] = {0, 0, 0};
    int32_t maxabs[3] = {0, 0, 0}, lo[3], hi[3], prev[3] = {0, 0, 0};
    int64_t mag_sumsq = 0;
    uint32_t mag_peak = 0;
    uint64_t mag_sum = 0;

    for (int i = 0; i < FEAT_WINDOW; i++) {
        int64_t sq = 0;
        for (int a = 0; a < 3; a++) {
            int32_t d = (int32_t)win[i][a] - baseline[a];
            sum[a] += d;
            sumsq[a] += (int64_t)d * d;
            sq += (int64_t)d * d;
            int32_t ad = d < 0 ? -d : d;
            if (ad > maxabs[a]) maxabs[a] = ad;
            if (i == 0) {
                lo[a] = hi[a] = d;
            } else {
                int32_t dj = d - prev[a];
                jerk[a] += dj < 0 ? -dj : dj;
                if (d < lo[a]) lo[a] = d;
                if (d > hi[a]) hi[a] = d;
            }
            prev[a] = d;
        }
        uint32_t m = isqrt64((uint64_t)sq);
        mag_sum += m;
        mag_sumsq += sq;
        if (m > mag_peak) mag_peak = m;
    }

    int32_t ptp = 0;
    for (int a = 0; a < 3; a++) {
        int64_t mean = sum[a] >> 6;               /* arithmetic shift = floor */
        int64_t var = (sumsq[a] >> 6) - mean * mean;
        if (var < 0) var = 0;
        out[a] = (int32_t)mean;
        out[3 + a] = (int32_t)isqrt64((uint64_t)var);
        out[6 + a] = maxabs[a];
        out[9 + a] = (int32_t)(jerk[a] / (FEAT_WINDOW - 1));
        ptp += hi[a] - lo[a];
    }
    out[12] = (int32_t)mag_peak;
    out[13] = (int32_t)(mag_sum >> 6);
    out[14] = (int32_t)isqrt64((uint64_t)mag_sumsq >> 6);
    out[15] = ptp;
}

void features_quantise(const int32_t feat[FEAT_COUNT], int8_t out[FEAT_COUNT])
{
    for (int i = 0; i < FEAT_COUNT; i++) {
        int64_t f = feat[i];
        if (f > 32767) f = 32767;
        if (f < -32768) f = -32768;
        int64_t q = (f * model_feat_mult[i] + (1 << (MODEL_FEAT_SHIFT - 1))) >> MODEL_FEAT_SHIFT;
        out[i] = (int8_t)(q > 127 ? 127 : (q < -128 ? -128 : q));
    }
}

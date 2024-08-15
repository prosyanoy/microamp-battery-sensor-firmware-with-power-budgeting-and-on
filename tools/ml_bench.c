/* Model size and cycle report (host build). Prints flash/RAM footprint, MAC
 * counts, an estimated Cortex-M4 cycle budget and the measured host time. */
#include <stdio.h>
#include <time.h>

#include "detector_core.h"
#include "energy.h"
#include "ml_features.h"
#include "model_data.h"
#include "nn_int8.h"

int main(void)
{
    energy_params_t ep;
    energy_default_params(&ep);

    int32_t flash_w = 0, flash_b = 0;
    for (int l = 0; l < MODEL_N_LAYERS; l++) {
        flash_w += model_dims[l] * model_dims[l + 1];
        flash_b += 4 * model_dims[l + 1];
    }
    const int flash_misc = 4 * MODEL_N_IN + MODEL_N_LAYERS + 1;

    int16_t win[FEAT_WINDOW][3], base[3] = {0, 0, 1000};
    for (int i = 0; i < FEAT_WINDOW; i++) {
        win[i][0] = (int16_t)((i * 37) % 300 - 150);
        win[i][1] = (int16_t)((i * 53) % 200 - 100);
        win[i][2] = (int16_t)(1000 + (i * 11) % 60);
    }
    int32_t f[FEAT_COUNT];
    int8_t q[FEAT_COUNT];
    nn_result_t r;
    const int N = 20000;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < N; i++) {
        win[i % FEAT_WINDOW][0] = (int16_t)(i & 0x1FF);
        features_extract((const int16_t(*)[3])win, base, f);
        features_quantise(f, q);
        nn_infer(q, &r);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ns = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / N;

    uint32_t feat_cycles = FEAT_WINDOW * ep.cyc_per_feat_sample;
    uint32_t nn_cycles = r.macs * ep.cyc_per_mac;
    printf("== Microamp model report ==\n");
    printf("architecture        : %d -> ", MODEL_N_IN);
    for (int l = 0; l < MODEL_N_LAYERS; l++) printf("%d%s", model_dims[l + 1], l + 1 < MODEL_N_LAYERS ? " -> " : "\n");
    printf("weights (int8)      : %d bytes\n", flash_w);
    printf("biases (int32)      : %d bytes\n", flash_b);
    printf("feature multipliers : %d bytes (+%d misc)\n", 4 * MODEL_N_IN, flash_misc - 4 * MODEL_N_IN);
    printf("flash total (model) : %d bytes\n", flash_w + flash_b + flash_misc);
    printf("RAM scratch (infer) : %d bytes activations + %d bytes input\n", 2 * MODEL_MAX_WIDTH, FEAT_COUNT);
    printf("RAM detector window : %zu bytes (static, no heap)\n", sizeof(detector_t));
    printf("MACs / inference    : %u\n", r.macs);
    printf("cycles / window     : ~%u features + ~%u network = ~%u (Cortex-M4 estimate: %u cyc/MAC, %u cyc/sample)\n",
           feat_cycles, nn_cycles, feat_cycles + nn_cycles, ep.cyc_per_mac, ep.cyc_per_feat_sample);
    printf("time @ %u MHz       : %.2f ms per window (stride %d ms -> %.2f%% CPU duty)\n", ep.cpu_hz / 1000000,
           (feat_cycles + nn_cycles) * 1000.0 / ep.cpu_hz, DET_STRIDE * DET_SAMPLE_PERIOD_MS,
           100.0 * (feat_cycles + nn_cycles) / ep.cpu_hz / (DET_STRIDE * DET_SAMPLE_PERIOD_MS / 1000.0));
    printf("host measurement    : %.0f ns per window (features + quantise + network)\n", ns);
    return 0;
}

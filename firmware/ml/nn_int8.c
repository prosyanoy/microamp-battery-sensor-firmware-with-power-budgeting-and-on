#include "nn_int8.h"

#include <stddef.h>

static const int8_t *const W[MODEL_N_LAYERS] = {model_w1, model_w2, model_w3};
static const int32_t *const B[MODEL_N_LAYERS] = {model_b1, model_b2, model_b3};
static const int32_t M[MODEL_N_LAYERS] = {MODEL_L1_MULT, MODEL_L2_MULT, MODEL_L3_MULT};
static const uint8_t RELU[MODEL_N_LAYERS] = {MODEL_L1_RELU, MODEL_L2_RELU, MODEL_L3_RELU};

void nn_infer(const int8_t in[MODEL_N_IN], nn_result_t *out)
{
    int8_t buf[2][MODEL_MAX_WIDTH];           /* ping-pong activations */
    const int8_t *src = in;
    uint32_t macs = 0;

    for (int l = 0; l < MODEL_N_LAYERS; l++) {
        const int n_in = model_dims[l], n_out = model_dims[l + 1];
        int8_t *dst = buf[l & 1];
        const int8_t *w = W[l];
        for (int o = 0; o < n_out; o++) {
            int32_t acc = B[l][o];
            for (int i = 0; i < n_in; i++) acc += (int32_t)w[o * n_in + i] * src[i];
            macs += (uint32_t)n_in;
            int64_t r = ((int64_t)acc * M[l] + (1 << (MODEL_REQ_SHIFT - 1))) >> MODEL_REQ_SHIFT;
            int lo = RELU[l] ? 0 : -128;
            dst[o] = (int8_t)(r > 127 ? 127 : (r < lo ? lo : r));
        }
        src = dst;
    }

    int best = 0;
    for (int c = 0; c < MODEL_N_CLASSES; c++) {
        out->logits[c] = src[c];
        if (src[c] > src[best]) best = c;
    }
    int second = -129;
    for (int c = 0; c < MODEL_N_CLASSES; c++)
        if (c != best && src[c] > second) second = src[c];
    out->cls = (uint8_t)best;
    out->margin = (uint8_t)(src[best] - second);
    out->macs = macs;
}

#ifndef NN_INT8_H
#define NN_INT8_H

#include <stdint.h>

#include "model_data.h"

typedef struct {
    int8_t logits[MODEL_N_CLASSES];
    uint8_t cls;
    uint8_t margin;      /* best - second best logit, 0..255 */
    uint32_t macs;       /* multiply-accumulates executed (for the cycle model) */
} nn_result_t;

/* Fixed-point int8 MLP. Statically allocated scratch only: no heap, no float. */
void nn_infer(const int8_t in[MODEL_N_IN], nn_result_t *out);

#endif

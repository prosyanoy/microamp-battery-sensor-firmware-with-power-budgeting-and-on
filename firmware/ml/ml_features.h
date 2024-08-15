#ifndef ML_FEATURES_H
#define ML_FEATURES_H

#include <stdint.h>

#define FEAT_WINDOW 64
#define FEAT_COUNT 16

/* Integer-only features over a 64-sample window of (x,y,z) milli-g, relative
 * to a gravity baseline. Mirrors ml/features.py exactly. */
void features_extract(const int16_t win[FEAT_WINDOW][3], const int16_t baseline[3],
                      int32_t out[FEAT_COUNT]);

/* Scale features to int8 with the per-feature fixed-point multipliers. */
void features_quantise(const int32_t feat[FEAT_COUNT], int8_t out[FEAT_COUNT]);

uint32_t isqrt64(uint64_t v);

#endif

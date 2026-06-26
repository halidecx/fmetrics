/*
 * Copyright © 2026, Halide Compression, LLC.
 * All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef FMETRICS_UTIL_H
#define FMETRICS_UTIL_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <math.h>

static inline int imin(const int a, const int b) { return a < b ? a : b; }
static inline int imax(const int a, const int b) { return a > b ? a : b; }

static inline int iclip(const int v, const int min, const int max) {
    return v < min ? min : v > max ? max : v;
}

static inline double fclip(const double v, const double min, const double max) {
    return fmin(fmax(v, min), max);
}

static inline float fclipf(const float v, const float min, const float max) {
    return fmin(fmax(v, min), max);
}

static inline float flog2(const double v) {
    const union { const double d; const uint64_t x; } u = { v };
    const uint64_t mt = u.x & ((1ULL << 52) - 1);
    return (float)((u.x >> 52) & 0x7FF) - 1023.0 +
        (float)mt * (1.0 / (1ULL << 52));
}

#endif /* FMETRICS_UTIL_H */

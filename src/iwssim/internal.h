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
#ifndef IWSSIM_INTERNAL_H
#define IWSSIM_INTERNAL_H

#include <stdint.h>
#include <stddef.h>

#define restrict __restrict__

#define IWSSIM_NSCALES 5
#define IWSSIM_WINSIZE 11
#define IWSSIM_BLOCK 3
#define IWSSIM_MAX_NEIGHBORS 10

#define SIGMA 1.5
#define C1 6.502500057220458984375f // (float)((0.01 * 255.0) * (0.01 * 255.0))
#define C2 58.52249908447265625f    // (float)((0.03 * 255.0) * (0.03 * 255.0))

#define TOL 1e-15f
#define INV9 1.0f / 9.0f
#define SIGMA_NSQ 0.4
#define INV_SIGMA_NSQ_SQ 1.0 / (SIGMA_NSQ * SIGMA_NSQ)

static const double MS_SSIM_WEIGHTS[IWSSIM_NSCALES] = {
    0.0448, 0.2856, 0.3001, 0.2363, 0.1333,
};

static const float BURT_KRNL[5] = {
    0.08838834764831845f,
    0.3535533905932738f,
    0.5303300858899107f,
    0.3535533905932738f,
    0.08838834764831845f,
};

typedef struct ImageD {
    int width;
    int height;
    float *data;
} ImageD;

typedef struct Pyramid {
    ImageD bands[IWSSIM_NSCALES];
} Pyramid;

typedef struct ScratchBuffer {
    void *data;
    size_t size;
    size_t offset;
} ScratchBuffer;

typedef struct ScratchMark {
    size_t offset;
} ScratchMark;

typedef struct Enlarge2Coeff {
    int i0, i1;
    float w;
} Enlarge2Coeff;

static inline void *scratch_alloc(ScratchBuffer *const s,
                                  const size_t byte_count)
{
    size_t aligned = (s->offset + 63) & ~(size_t)63;
    if (aligned + byte_count > s->size) return NULL;
    void *ptr = (char *)s->data + aligned;
    s->offset = aligned + byte_count;
    return ptr;
}

static inline ScratchMark scratch_mark(const ScratchBuffer *const s) {
    ScratchMark m;
    m.offset = s->offset;
    return m;
}

static inline void scratch_reset(ScratchBuffer *const s, const ScratchMark m) {
    if (s->offset > m.offset) s->offset = m.offset;
}

#endif /* IWSSIM_INTERNAL_H */

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
#ifndef FMETRICS_MEM_H
#define FMETRICS_MEM_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <math.h>

typedef struct ImageD {
    int width;
    int height;
    float *data;
} ImageD;

typedef struct ScratchBuffer {
    void *data;
    size_t size;
    size_t offset;
} ScratchBuffer;

typedef struct ScratchMark {
    size_t offset;
} ScratchMark;

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

static bool image_alloc(ImageD *const im, const int width, const int height,
                        ScratchBuffer *const scratch)
{
    if (width <= 0 || height <= 0) return false;
    const size_t pixels = (size_t)width * (size_t)height;
    im->data = (float*)scratch_alloc(scratch, pixels * sizeof(*im->data));
    if (im->data == NULL) return false;
    im->width = width;
    im->height = height;
    return true;
}

#endif /* FMETRICS_MEM_H */

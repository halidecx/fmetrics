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
#ifndef FMETRICS_H
#define FMETRICS_H

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Version */
static const char* FMETRICS_VERSION = "0.0.1";

/**
 * Get version string
 *
 * @return Version string (e.g., "X.Y.Z")
 */
static const char* fmetrics_version_str(void) {
    return FMETRICS_VERSION;
}

typedef enum FmetricsErr {
    FMETRICS_OK = 0,
    FMETRICS_ERR_INVALID_ARGUMENT = 1,
    FMETRICS_ERR_UNSUPPORTED_FORMAT = 2,
    FMETRICS_ERR_DIMENSION_MISMATCH = 3,
    FMETRICS_ERR_OUT_OF_MEMORY = 4,
    FMETRICS_ERR_IWSSIM_IMG_TOO_SMALL = 5,
    FMETRICS_ERR_INTERNAL = 6,
} FmetricsErr;

typedef enum FmetricsPixFmt {
    FMETRICS_PIX_FMT_RGB_UINT8 = 1,
} FmetricsPixFmt;

typedef enum FmetricsColorspace {
    FMETRICS_COLORSPACE_SRGB = 1,
} FmetricsColorspace;

typedef struct FmetricsImg {
    const uint8_t *data;
    uint32_t width, height, stride;
    FmetricsPixFmt format;
    FmetricsColorspace colorspace;
} FmetricsImg;

/**
 * Get error message string
 *
 * @param error Error code
 *
 * @return Human-readable error message
 */
const char *fmetrics_error_str(const FmetricsErr err);

FmetricsErr fmetrics_iwssim_cmp(const FmetricsImg *const reference,
                                const FmetricsImg *const distorted,
                                double *const result);

FmetricsErr fmetrics_ssimu2_cmp(const FmetricsImg *const reference,
                                const FmetricsImg *const distorted,
                                double *const result);

FmetricsErr fmetrics_ssimu2_cmp_map(const FmetricsImg *const reference,
                                    const FmetricsImg *const distorted,
                                    double *const result,
                                    uint32_t *const error_map);

#ifdef __cplusplus
}
#endif

#endif /* FMETRICS_H */

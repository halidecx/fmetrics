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

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Version */
static const char* FMETRICS_VERSION = "0.0.2";

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
    FMETRICS_PIX_FMT_RGB_FLOAT = 2,
    FMETRICS_PIX_FMT_RGB_UINT16 = 3,
} FmetricsPixFmt;

typedef enum FmetricsColorspace {
    FMETRICS_COLORSPACE_SRGB = 1,
} FmetricsColorspace;

typedef struct FmetricsImg {
    const void *data;
    uint32_t width, height, stride;
    FmetricsPixFmt format;
    FmetricsColorspace colorspace;
} FmetricsImg;

typedef struct FmetricsButteraugliOptions {
    float intensity_target;
    int pnorm;
} FmetricsButteraugliOptions;

typedef enum FmetricsCvvdpDisplayModel {
    FMETRICS_CVVDP_DISPLAY_STANDARD_FHD = 0,
    FMETRICS_CVVDP_DISPLAY_STANDARD_4K,
    FMETRICS_CVVDP_DISPLAY_STANDARD_HDR_PQ,
    FMETRICS_CVVDP_DISPLAY_STANDARD_HDR_HLG,
    FMETRICS_CVVDP_DISPLAY_STANDARD_HDR_LINEAR,
    FMETRICS_CVVDP_DISPLAY_STANDARD_HDR_DARK,
    FMETRICS_CVVDP_DISPLAY_STANDARD_HDR_LINEAR_ZOOM,
    FMETRICS_CVVDP_DISPLAY_STANDARD_HMD,
    FMETRICS_CVVDP_DISPLAY_STANDARD_PHONE,
    FMETRICS_CVVDP_DISPLAY_SDR_4K_30,
    FMETRICS_CVVDP_DISPLAY_SDR_FHD_24,
    FMETRICS_CVVDP_DISPLAY_HTC_VIVE_PRO,
    FMETRICS_CVVDP_DISPLAY_IPHONE_12_PRO,
    FMETRICS_CVVDP_DISPLAY_IPHONE_14_PRO,
    FMETRICS_CVVDP_DISPLAY_IPHONE_14_PRO_VERT,
    FMETRICS_CVVDP_DISPLAY_IPHONE_14_PRO_HDR,
    FMETRICS_CVVDP_DISPLAY_IPHONE_14_PRO_HDR_VERT,
    FMETRICS_CVVDP_DISPLAY_IPAD_PRO_12_9,
    FMETRICS_CVVDP_DISPLAY_MACBOOK_PRO_16,
    FMETRICS_CVVDP_DISPLAY_LG_OLED_2017_SDR,
    FMETRICS_CVVDP_DISPLAY_LG_OLED_2017_HDR,
    FMETRICS_CVVDP_DISPLAY_EIZO_CG3146,
    FMETRICS_CVVDP_DISPLAY_65INCH_HDR_PQ_4KNIT,
    FMETRICS_CVVDP_DISPLAY_65INCH_HDR_PQ_2KNIT,
    FMETRICS_CVVDP_DISPLAY_65INCH_HDR_PQ_1KNIT,
    FMETRICS_CVVDP_DISPLAY_LG_OLED_2026_HDR_PQ,
    FMETRICS_CVVDP_DISPLAY_CID22_MCOS,
    FMETRICS_CVVDP_DISPLAY_CUSTOM,
} FmetricsCvvdpDisplayModel;

typedef struct FmetricsCvvdpDisplayParams {
    int resolution_width, resolution_height;
    float viewing_distance_meters;
    float diagonal_size_inches;
    float max_luminance;
    float contrast;
    float ambient_light;
    float reflectivity;
    bool is_hdr;
} FmetricsCvvdpDisplayParams;

typedef struct FmetricsCvvdpResult {
    double jod;
    double quality;
} FmetricsCvvdpResult;

typedef struct FmetricsCvvdpCtx FmetricsCvvdpCtx;

FmetricsErr fmetrics_cvvdp_create(
    int width, int height, float fps,
    FmetricsCvvdpDisplayModel display_model, unsigned threads,
    const FmetricsCvvdpDisplayParams *custom_params,
    FmetricsCvvdpCtx **out_context);
void fmetrics_cvvdp_destroy(FmetricsCvvdpCtx *context);
FmetricsErr fmetrics_cvvdp_process_frame(
    FmetricsCvvdpCtx *context, const FmetricsImg *reference,
    const FmetricsImg *distorted, FmetricsCvvdpResult *result);
FmetricsErr fmetrics_cvvdp_reset(FmetricsCvvdpCtx *context);
FmetricsErr fmetrics_cvvdp_cmp(
    const FmetricsImg *reference,
    const FmetricsImg *distorted,
    FmetricsCvvdpDisplayModel display_model, unsigned threads,
    const FmetricsCvvdpDisplayParams *custom_params,
    FmetricsCvvdpResult *result);
FmetricsErr fmetrics_cvvdp_get_display_params(
    FmetricsCvvdpDisplayModel model,
    FmetricsCvvdpDisplayParams *out_params);
const char *fmetrics_cvvdp_version_str(void);

typedef struct FmetricsWorkspace FmetricsWorkspace;

FmetricsWorkspace *fmetrics_workspace_create(void);
void fmetrics_workspace_destroy(FmetricsWorkspace *workspace);

/**
 * Get error message string
 *
 * @param error Error code
 *
 * @return Human-readable error message
 */
const char *fmetrics_error_str(const FmetricsErr err);

/**
 * Compare two images using IW-SSIM
 *
 * @param workspace Workspace for scratch allocations
 * @param reference Reference image
 * @param distorted Distorted image
 * @param result Output IW-SSIM score
 *
 * @return FMETRICS_OK on success, error code otherwise
 */
FmetricsErr fmetrics_iwssim_cmp(FmetricsWorkspace *const workspace,
                                const FmetricsImg *const reference,
                                const FmetricsImg *const distorted,
                                double *const result);

/**
 * Compare two images using MS-SSIM
 *
 * @param workspace Workspace for scratch allocations
 * @param reference Reference image
 * @param distorted Distorted image
 * @param result Output MS-SSIM score
 *
 * @return FMETRICS_OK on success, error code otherwise
 */
FmetricsErr fmetrics_msssim_cmp(FmetricsWorkspace *const workspace,
                                const FmetricsImg *const reference,
                                const FmetricsImg *const distorted,
                                double *const result);

/**
 * Compare two images using SSIMULACRA2
 *
 * @param workspace Workspace for scratch allocations
 * @param reference Reference image
 * @param distorted Distorted image
 * @param result Output SSIMULACRA2 score
 *
 * @return FMETRICS_OK on success, error code otherwise
 */
FmetricsErr fmetrics_ssimu2_cmp(FmetricsWorkspace *const workspace,
                                const FmetricsImg *const reference,
                                const FmetricsImg *const distorted,
                                double *const result);

/**
 * Compare two images using SSIMULACRA2 with an error map
 *
 * @param workspace Workspace for scratch allocations
 * @param reference Reference image
 * @param distorted Distorted image
 * @param result Output SSIMULACRA2 score
 * @param error_map Output error map (width*height entries)
 *
 * @return FMETRICS_OK on success, error code otherwise
 */
FmetricsErr fmetrics_ssimu2_cmp_map(FmetricsWorkspace *const workspace,
                                    const FmetricsImg *const reference,
                                    const FmetricsImg *const distorted,
                                    double *const result,
                                    uint32_t *const error_map);

/**
 * Compare two images using Butteraugli
 *
 * @param workspace Workspace for scratch allocations
 * @param reference Reference image
 * @param distorted Distorted image
 * @param o Butteraugli options
 * @param result Output Butteraugli score
 *
 * @return FMETRICS_OK on success, error code otherwise
 */
FmetricsErr fmetrics_butteraugli_cmp(FmetricsWorkspace *const workspace,
                                     const FmetricsImg *const reference,
                                     const FmetricsImg *const distorted,
                                     const FmetricsButteraugliOptions *const o,
                                     double *const result);

/**
 * Compare two images using Butteraugli with an error map
 *
 * @param workspace Workspace for scratch allocations
 * @param reference Reference image
 * @param distorted Distorted image
 * @param o Butteraugli options
 * @param result Output Butteraugli score
 * @param error_map Output error map (width*height entries)
 *
 * @return FMETRICS_OK on success, error code otherwise
 */
FmetricsErr fmetrics_butteraugli_cmp_map(
                                         FmetricsWorkspace *const workspace,
                                         const FmetricsImg *const reference,
                                         const FmetricsImg *const distorted,
                                         const FmetricsButteraugliOptions
                                         *const o, double *const result,
                                         uint32_t *const error_map);

#ifdef __cplusplus
}
#endif

#endif /* FMETRICS_H */

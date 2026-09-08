#ifndef FMETRICS_COLOR_H
#define FMETRICS_COLOR_H

#include "../fmetrics.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static inline bool fmetrics_is_linear(const FmetricsImg *img) {
    return img->format == FMETRICS_PIX_FMT_RGB_FLOAT &&
        img->colorspace == FMETRICS_COLORSPACE_LINEAR_SRGB;
}

static inline FmetricsErr fmetrics_validate_rgb(const FmetricsImg *img) {
    const bool linear = fmetrics_is_linear(img);
    if (!linear && !(img->format == FMETRICS_PIX_FMT_RGB_UINT8 &&
        img->colorspace == FMETRICS_COLORSPACE_SRGB))
        return FMETRICS_ERR_UNSUPPORTED_FORMAT;
    if (img->stride < (size_t)img->width * (linear ? 12 : 3))
        return FMETRICS_ERR_INVALID_ARGUMENT;
    if (linear) for (uint32_t y = 0; y < img->height; y++) {
        const uint8_t *row = (const uint8_t *)img->data +
            (size_t)y * img->stride;
        for (size_t x = 0; x < (size_t)img->width * 3; x++) {
            uint32_t bits;
            memcpy(&bits, row + x * sizeof(float), sizeof(bits));
            if ((bits & 0x7f800000u) == 0x7f800000u)
                return FMETRICS_ERR_INVALID_ARGUMENT;
        }
    }
    return FMETRICS_OK;
}

static inline float fmetrics_srgb_linear(const float v) {
    return v <= 0.04045f ? v / 12.92f :
        powf((v + 0.055f) / 1.055f, 2.4f);
}

static inline void fmetrics_linear_rgb(const FmetricsImg *img,
                                       size_t x, size_t y, float rgb[3])
{
    const uint8_t *row = (const uint8_t *)img->data + y * img->stride;
    if (fmetrics_is_linear(img)) memcpy(rgb, row + x * 12, 12);
    else for (unsigned ch = 0; ch < 3; ch++)
        rgb[ch] = fmetrics_srgb_linear(row[x * 3 + ch] / 255.0f);
}

static inline double fmetrics_sdr_luma(const FmetricsImg *img,
                                       size_t x, size_t y)
{
    float rgb[3];
    fmetrics_linear_rgb(img, x, y, rgb);
    double encoded[3];
    for (unsigned ch = 0; ch < 3; ch++) {
        const double v = fabs(rgb[ch]);
        encoded[ch] = copysign(v <= 0.0031308 ? 12.92 * v :
            1.055 * pow(v, 1.0 / 2.4) - 0.055, rgb[ch]) * 255.0;
    }
    return 0.299 * encoded[0] + 0.587 * encoded[1] + 0.114 * encoded[2];
}

static inline float fmetrics_pq_luma(const FmetricsImg *img,
                                    size_t x, size_t y)
{
    float rgb[3];
    fmetrics_linear_rgb(img, x, y, rgb);
    const double luminance = fmax(0.0, 0.2126 * rgb[0] +
        0.7152 * rgb[1] + 0.0722 * rgb[2]) * 203.0;
    const double p = pow(luminance / 10000.0, 2610.0 / 16384.0);
    return (float)pow((3424.0 / 4096.0 + 2413.0 / 128.0 * p) /
                      (1.0 + 2392.0 / 128.0 * p), 2523.0 / 32.0);
}

#endif

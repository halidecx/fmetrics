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
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "../common/util.h"
#include "../fmetrics.h"
#include "internal.h"

static float srgb_lut[256];
static uint32_t turbo_lut[256];
static pthread_once_t tables_once = PTHREAD_ONCE_INIT;

static uint32_t turbo_color(const float x) {
    const float t = fclipf(x, 0.0, 1.0);
    const float t2 = t * t;
    const float t3 = t2 * t;
    const float t4 = t2 * t2;
    const float t5 = t3 * t2;
    const float r = 0.13572138f + 4.61539260f * t - 42.66032258f * t2 +
                    132.13108234f * t3 - 152.94239396f * t4 +
                    59.28637943f * t5;
    const float g = 0.09140261f + 2.19418839f * t + 4.84296658f * t2 -
                    14.18503333f * t3 + 4.27729857f * t4 +
                    2.82956604f * t5;
    const float b = 0.10667330f + 12.64194608f * t - 60.58204836f * t2 +
                    110.36276771f * t3 - 89.90310912f * t4 +
                    27.34824973f * t5;
    return (uint32_t)f32_to_u8(r) |
           ((uint32_t)f32_to_u8(g) << 8) |
           ((uint32_t)f32_to_u8(b) << 16) |
           0xff000000u;
}

static void init_tables_impl(void) {
    for (int i = 0; i < 256; i++) {
        const float c = (float)i / 255.0f;
        srgb_lut[i] = c <= 0.04045f ? c / 12.92f :
            powf((c + 0.055f) / 1.055f, 2.4f);
        turbo_lut[i] = turbo_color((float)i / 255.0f);
    }
}

static void init_tables(void) {
    pthread_once(&tables_once, init_tables_impl);
}

static FmetricsErr validate_image_pair(const FmetricsImg *const reference,
                                       const FmetricsImg *const distorted)
{
    if (reference == NULL || distorted == NULL || reference->data == NULL ||
        distorted->data == NULL)
    {
        return FMETRICS_ERR_INVALID_ARGUMENT;
    }
    if (reference->format != FMETRICS_PIX_FMT_RGB_UINT8 ||
        distorted->format != FMETRICS_PIX_FMT_RGB_UINT8 ||
        reference->colorspace != FMETRICS_COLORSPACE_SRGB ||
        distorted->colorspace != FMETRICS_COLORSPACE_SRGB)
    {
        return FMETRICS_ERR_UNSUPPORTED_FORMAT;
    }
    if (reference->width != distorted->width ||
        reference->height != distorted->height)
    {
        return FMETRICS_ERR_DIMENSION_MISMATCH;
    }
    if (reference->width == 0 || reference->height == 0 ||
        reference->stride < reference->width * 3 ||
        distorted->stride < distorted->width * 3)
    {
        return FMETRICS_ERR_INVALID_ARGUMENT;
    }
    return FMETRICS_OK;
}

static inline double get_weight(const int c, const int scale, const int map,
                                const int norm)
{
    return WEIGHT[36 * c + 6 * scale + 3 * norm + map];
}

static inline double fourth(const double y) {
    const double x = y * y;
    return x * x;
}

static void rgb_to_planar_linear(const FmetricsImg *const src,
                                 float *const planes[SSIMU2_PLANES])
{
    for (uint32_t y = 0; y < src->height; y++) {
        const uint8_t *restrict row = src->data + (size_t)y * src->stride;
        const size_t out_row = (size_t)y * src->width;
        for (uint32_t x = 0; x < src->width; x++) {
            const uint8_t *restrict px = row + (size_t)x * 3u;
            planes[0][out_row + x] = srgb_lut[px[0]];
            planes[1][out_row + x] = srgb_lut[px[1]];
            planes[2][out_row + x] = srgb_lut[px[2]];
        }
    }
}

static inline void linear_rgb_to_xyb(const float r, const float g,
                                     const float b, float out[3])
{
    const float bias = 0.0037930734f;
    const float absorbance_bias = -cbrtf(bias);
    float mixed[3];
    mixed[0] = OPSIN_MATRIX[0] * r + OPSIN_MATRIX[1] * g +
               OPSIN_MATRIX[2] * b + bias;
    mixed[1] = OPSIN_MATRIX[3] * r + OPSIN_MATRIX[4] * g +
               OPSIN_MATRIX[5] * b + bias;
    mixed[2] = OPSIN_MATRIX[6] * r + OPSIN_MATRIX[7] * g +
               OPSIN_MATRIX[8] * b + bias;

    for (int i = 0; i < 3; i++) mixed[i] = cbrtf(fmaxf(mixed[i], 0.0f)) + absorbance_bias;

    out[0] = 0.5f * (mixed[0] - mixed[1]);
    out[1] = 0.5f * (mixed[0] + mixed[1]);
    out[2] = mixed[2];
    out[2] = (out[2] - out[1]) + 0.55f;
    out[0] = out[0] * 14.0f + 0.42f;
    out[1] = out[1] + 0.01f;
}

static void to_xyb(float *const src[SSIMU2_PLANES],
                   float *const dst[SSIMU2_PLANES],
                   const uint32_t stride, const uint32_t w,
                   const uint32_t h)
{
    for (uint32_t y = 0; y < h; y++) {
        const size_t row = (size_t)y * stride;
        for (uint32_t x = 0; x < w; x++) {
            float xyb[3];
            const size_t idx = row + x;
            linear_rgb_to_xyb(src[0][idx], src[1][idx], src[2][idx], xyb);
            dst[0][idx] = xyb[0];
            dst[1][idx] = xyb[1];
            dst[2][idx] = xyb[2];
        }
    }
}

static void multiply(const float *restrict a, const float *restrict b,
                     float *restrict dst, const uint32_t stride,
                     const uint32_t w, const uint32_t h)
{
    for (uint32_t y = 0; y < h; y++) {
        const size_t row = (size_t)y * stride;
        for (uint32_t x = 0; x < w; x++) dst[row + x] = a[row + x] * b[row + x];
    }
}

static void blur_h(const float *restrict src, float *restrict dst,
                   const uint32_t w)
{
    uint32_t j = 0;
    const uint32_t left_end = (uint32_t)imin((int)w, SSIMU2_RADIUS);
    for (; j < left_end; j++) {
        const int jj = (int)j;
        const int dist_right = (int)w - 1 - jj;
        float sum = 0.0f;
        for (int k = 0; k < SSIMU2_KSIZE; k++) {
            int idx;
            if (k < SSIMU2_RADIUS) {
                idx = jj < SSIMU2_RADIUS - k ?
                    imin(SSIMU2_RADIUS - k - jj, (int)w - 1) :
                    jj - SSIMU2_RADIUS + k;
            } else {
                idx = dist_right < k - SSIMU2_RADIUS ?
                    jj - imin(k - SSIMU2_RADIUS - dist_right, jj) :
                    jj - SSIMU2_RADIUS + k;
            }
            sum += GAUSSIAN[k] * src[idx];
        }
        dst[j] = sum;
    }

    const uint32_t interior_end = w - (uint32_t)imin((int)w, SSIMU2_RADIUS);
    for (j = SSIMU2_RADIUS; j < interior_end; j++) {
        const float *restrict p = src + j - SSIMU2_RADIUS;
        dst[j] = GAUSSIAN[0] * p[0] + GAUSSIAN[1] * p[1] +
                 GAUSSIAN[2] * p[2] + GAUSSIAN[3] * p[3] +
                 GAUSSIAN[4] * p[4] + GAUSSIAN[5] * p[5] +
                 GAUSSIAN[6] * p[6] + GAUSSIAN[7] * p[7] +
                 GAUSSIAN[8] * p[8];
    }

    const uint32_t right_start = (uint32_t)imax(SSIMU2_RADIUS, (int)interior_end);
    for (j = right_start; j < w; j++) {
        const int jj = (int)j;
        const int dist_right = (int)w - 1 - jj;
        float sum = 0.0f;
        for (int k = 0; k < SSIMU2_KSIZE; k++) {
            int idx;
            if (k < SSIMU2_RADIUS) {
                idx = jj < SSIMU2_RADIUS - k ?
                    imin(SSIMU2_RADIUS - k - jj, (int)w - 1) :
                    jj - SSIMU2_RADIUS + k;
            } else {
                idx = dist_right < k - SSIMU2_RADIUS ?
                    jj - imin(k - SSIMU2_RADIUS - dist_right, jj) :
                    jj - SSIMU2_RADIUS + k;
            }
            sum += GAUSSIAN[k] * src[idx];
        }
        dst[j] = sum;
    }
}

static void blur(const float *restrict src, float *restrict dst,
                 const uint32_t stride, const uint32_t w, const uint32_t h,
                 float *restrict tmp_row)
{
    const int ih = (int)h;
    for (int i = 0; i < ih; i++) {
        const int dist_bottom = ih - 1 - i;
        const float *rows[SSIMU2_KSIZE];
        for (int k = 0; k < SSIMU2_KSIZE; k++) {
            int row;
            if (k < SSIMU2_RADIUS) {
                row = i < SSIMU2_RADIUS - k ?
                    imin(SSIMU2_RADIUS - k - i, ih - 1) :
                    i - SSIMU2_RADIUS + k;
            } else {
                row = dist_bottom < k - SSIMU2_RADIUS ?
                    i - imin(k - SSIMU2_RADIUS - dist_bottom, i) :
                    i - SSIMU2_RADIUS + k;
            }
            rows[k] = src + (size_t)row * stride;
        }
        for (uint32_t x = 0; x < w; x++) {
            tmp_row[x] = GAUSSIAN[0] * rows[0][x] + GAUSSIAN[1] * rows[1][x] +
                         GAUSSIAN[2] * rows[2][x] + GAUSSIAN[3] * rows[3][x] +
                         GAUSSIAN[4] * rows[4][x] + GAUSSIAN[5] * rows[5][x] +
                         GAUSSIAN[6] * rows[6][x] + GAUSSIAN[7] * rows[7][x] +
                         GAUSSIAN[8] * rows[8][x];
        }
        blur_h(tmp_row, dst + (size_t)i * stride, w);
    }
}

static void downscale(float *const src[SSIMU2_PLANES],
                      float *const dst[SSIMU2_PLANES],
                      const uint32_t src_stride, const uint32_t in_w,
                      const uint32_t in_h)
{
    const uint32_t out_w = (in_w + 1) >> 1;
    const uint32_t out_h = (in_h + 1) >> 1;
    const uint32_t dst_stride = (src_stride + 1) >> 1;
    for (uint32_t p = 0; p < SSIMU2_PLANES; p++) {
        const float *restrict srcp = src[p];
        float *restrict dstp = dst[p];
        for (uint32_t oy = 0; oy < out_h; oy++) {
            for (uint32_t ox = 0; ox < out_w; ox++) {
                const uint32_t x0 = imin((int)(ox * 2), (int)in_w - 1);
                const uint32_t x1 = imin((int)(ox * 2 + 1), (int)in_w - 1);
                const uint32_t y0 = imin((int)(oy * 2), (int)in_h - 1);
                const uint32_t y1 = imin((int)(oy * 2 + 1), (int)in_h - 1);
                dstp[(size_t)oy * dst_stride + ox] =
                    0.25f * (srcp[(size_t)y0 * src_stride + x0] +
                             srcp[(size_t)y0 * src_stride + x1] +
                             srcp[(size_t)y1 * src_stride + x0] +
                             srcp[(size_t)y1 * src_stride + x1]);
            }
        }
    }
}

static void ssim_map(const float *restrict s11, const float *restrict s22,
                     const float *restrict s12, const float *restrict mu1,
                     const float *restrict mu2, const uint32_t stride,
                     const uint32_t w, const uint32_t h, const uint32_t plane,
                     const double one_per_pixels, double *restrict out,
                     float *restrict error_scale, const uint32_t scale)
{
    double sum0 = 0.0;
    double sum1 = 0.0;
    const float weight0 = (float)get_weight((int)plane, (int)scale, 0, 0);
    const float weight1 = (float)get_weight((int)plane, (int)scale, 0, 1);
    for (uint32_t y = 0; y < h; y++) {
        const size_t row = (size_t)y * stride;
        for (uint32_t x = 0; x < w; x++) {
            const size_t idx = row + x;
            const float m1 = mu1[idx], m2 = mu2[idx];
            const float mdiff = m1 - m2;
            const double num_m = fmaf(mdiff, -mdiff, 1.0f);
            const double num_s = fmaf(s12[idx] - m1 * m2, 2.0f, 0.0009f);
            const double den_s = (s11[idx] - m1 * m1) + (s22[idx] - m2 * m2) + 0.0009f;
            const double d = fmax(1.0 - ((num_m * num_s) / den_s), 0.0);
            sum0 += d;
            sum1 += fourth(d);
            if (error_scale != NULL) error_scale[idx] += (weight0 + weight1) * (float)d;
        }
    }
    out[plane * 2] = one_per_pixels * sum0;
    out[plane * 2 + 1] = sqrt(sqrt(one_per_pixels * sum1));
}

static void edge_map(const float *restrict im1, const float *restrict im2,
                     const float *restrict mu1, const float *restrict mu2,
                     const uint32_t stride, const uint32_t w,
                     const uint32_t h, const uint32_t plane,
                     const double one_per_pixels, double *restrict out,
                     float *restrict error_scale, const uint32_t scale)
{
    double sum[4] = {0.0, 0.0, 0.0, 0.0};
    const float wa0 = (float)get_weight((int)plane, (int)scale, 1, 0);
    const float wa1 = (float)get_weight((int)plane, (int)scale, 1, 1);
    const float wd0 = (float)get_weight((int)plane, (int)scale, 2, 0);
    const float wd1 = (float)get_weight((int)plane, (int)scale, 2, 1);
    for (uint32_t y = 0; y < h; y++) {
        const size_t row = (size_t)y * stride;
        for (uint32_t x = 0; x < w; x++) {
            const size_t idx = row + x;
            const double d = (1.0 + fabsf(im2[idx] - mu2[idx])) /
                             (1.0 + fabsf(im1[idx] - mu1[idx])) - 1.0;
            const double artifact = fmax(d, 0.0);
            const double detail_lost = fmax(-d, 0.0);
            sum[0] += artifact;
            sum[1] += fourth(artifact);
            sum[2] += detail_lost;
            sum[3] += fourth(detail_lost);
            if (error_scale != NULL) {
                error_scale[idx] += (wa0 + wa1) * (float)artifact;
                error_scale[idx] += (wd0 + wd1) * (float)detail_lost;
            }
        }
    }
    out[plane * 4] = one_per_pixels * sum[0];
    out[plane * 4 + 1] = sqrt(sqrt(one_per_pixels * sum[1]));
    out[plane * 4 + 2] = one_per_pixels * sum[2];
    out[plane * 4 + 3] = sqrt(sqrt(one_per_pixels * sum[3]));
}

static float bilinear_sample(const float *restrict src, const float fx,
                             const float fy, const uint32_t stride,
                             const uint32_t w, const uint32_t h)
{
    const float x_scaled = fx * (float)w;
    const float y_scaled = fy * (float)h;
    const float x_floor = floorf(x_scaled);
    const float y_floor = floorf(y_scaled);
    const float x_frac = x_scaled - x_floor;
    const float y_frac = y_scaled - y_floor;
    const uint32_t ix0 = (uint32_t)fclipf(x_floor, 0.0f, (float)(w - 1));
    const uint32_t iy0 = (uint32_t)fclipf(y_floor, 0.0f, (float)(h - 1));
    const uint32_t ix1 = (uint32_t)fclipf(x_floor + 1.0f, 0.0f, (float)(w - 1));
    const uint32_t iy1 = (uint32_t)fclipf(y_floor + 1.0f, 0.0f, (float)(h - 1));
    const float f00 = src[(size_t)iy0 * stride + ix0];
    const float f10 = src[(size_t)iy0 * stride + ix1];
    const float f01 = src[(size_t)iy1 * stride + ix0];
    const float f11 = src[(size_t)iy1 * stride + ix1];
    const float l0 = f00 * (1.0f - x_frac) + f10 * x_frac;
    const float l1 = f01 * (1.0f - x_frac) + f11 * x_frac;
    return l0 * (1.0f - y_frac) + l1 * y_frac;
}

static void upscale_and_accumulate(const float *restrict src,
                                   float *restrict dst,
                                   const uint32_t src_stride,
                                   const uint32_t src_w,
                                   const uint32_t src_h,
                                   const uint32_t dst_stride,
                                   const uint32_t dst_w,
                                   const uint32_t dst_h)
{
    const float h_scale = 1.0f / (float)dst_h;
    const float w_scale = 1.0f / (float)dst_w;
    for (uint32_t y = 0; y < dst_h; y++) {
        const float fy = (float)y * h_scale;
        const size_t row = (size_t)y * dst_stride;
        for (uint32_t x = 0; x < dst_w; x++) {
            const float fx = (float)x * w_scale;
            dst[row + x] += bilinear_sample(src, fx, fy, src_stride, src_w, src_h);
        }
    }
}

static double score(const double avg_ssim[SSIMU2_SCALES][6],
                    const double avg_edge[SSIMU2_SCALES][12])
{
    double accum = 0.0;
    size_t idx = 0;
    for (int plane = 0; plane < 3; plane++) {
        for (int scale = 0; scale < 6; scale++) {
            for (int n = 0; n < 2; n++) {
                accum += WEIGHT[idx++] * fabs(avg_ssim[scale][plane * 2 + n]);
                accum += WEIGHT[idx++] * fabs(avg_edge[scale][plane * 4 + n]);
                accum += WEIGHT[idx++] * fabs(avg_edge[scale][plane * 4 + n + 2]);
            }
        }
    }
    accum *= 0.9562382616834844;
    accum = (6.248496625763138e-5 * accum * accum) * accum +
            2.326765642916932 * accum -
            0.020884521182843837 * accum * accum;
    return accum > 0.0 ? pow(accum, 0.6276336467831387) * -10.0 + 100.0 : 100.0;
}

static void generate_error_map(const float *restrict error_accum,
                               uint32_t *restrict error_map,
                               const uint32_t stride, const uint32_t w,
                               const uint32_t h)
{
    for (uint32_t y = 0; y < h; y++) {
        const size_t row = (size_t)y * stride;
        const size_t out_row = (size_t)y * w;
        for (uint32_t x = 0; x < w; x++) {
            float ssim = error_accum[row + x];
            ssim *= 0.9562382616834844f;
            ssim = 2.326765642916932f * ssim -
                   0.020884521182843837f * ssim * ssim +
                   6.248496625763138e-05f * ssim * ssim * ssim;
            ssim = ssim > 0.0f ? 100.0f - 10.0f * powf(ssim, 0.6276336467831387f) : 100.0f;
            ssim = 1.0f - ssim / 100.0f;
            const int value = (int)(255.0f * fclipf(ssim, 0.0f, 1.0f));
            error_map[out_row + x] = turbo_lut[value];
        }
    }
}

static FmetricsErr ssimu2_compute(const FmetricsImg *const reference,
                                  const FmetricsImg *const distorted,
                                  double *const result,
                                  uint32_t *const error_map)
{
    const FmetricsErr valid = validate_image_pair(reference, distorted);
    if (valid != FMETRICS_OK) return valid;

    init_tables();

    const size_t pixels = (size_t)reference->width * reference->height;
    float *planes = malloc(pixels * 6u * sizeof(*planes));
    float *temp = malloc(pixels * (error_map != NULL ? 20u : 18u) * sizeof(*temp));
    float *scratch = malloc(reference->width * sizeof(*scratch));
    if (planes == NULL || temp == NULL || scratch == NULL) {
        free(planes);
        free(temp);
        free(scratch);
        return FMETRICS_ERR_OUT_OF_MEMORY;
    }

    float *ref_planes[3], *dis_planes[3];
    for (int i = 0; i < 3; i++) ref_planes[i] = planes + pixels * (size_t)i;
    for (int i = 0; i < 3; i++) dis_planes[i] = planes + pixels * (size_t)(i + 3);
    rgb_to_planar_linear(reference, ref_planes);
    rgb_to_planar_linear(distorted, dis_planes);

    const uint32_t stride = reference->width;
    const uint32_t wh = stride * reference->height;
    float *buf[6][3];
    size_t off = 0;
    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 3; j++) {
            buf[i][j] = temp + off;
            off += wh;
        }
    }
    float *error_accum = NULL;
    float *error_scale = NULL;
    if (error_map != NULL) {
        error_accum = temp + off;
        off += wh;
        error_scale = temp + off;
        off += wh;
    }

    for (int p = 0; p < 3; p++) {
        memcpy(buf[0][p], ref_planes[p], pixels * sizeof(float));
        memcpy(buf[1][p], dis_planes[p], pixels * sizeof(float));
    }

    double avg_ssim[SSIMU2_SCALES][6];
    double avg_edge[SSIMU2_SCALES][12];
    uint32_t stride2 = stride;
    uint32_t w2 = reference->width;
    uint32_t h2 = reference->height;

    for (uint32_t scale = 0; scale < SSIMU2_SCALES; scale++) {
        if (scale > 0) {
            downscale(buf[0], buf[0], stride2, w2, h2);
            downscale(buf[1], buf[1], stride2, w2, h2);
            stride2 = (stride2 + 1) >> 1;
            w2 = (w2 + 1) >> 1;
            h2 = (h2 + 1) >> 1;
        }

        const uint32_t current_wh = stride2 * h2;
        if (error_scale != NULL) memset(error_scale, 0, current_wh * sizeof(float));

        const double one_per_pixels = 1.0 / (double)(w2 * h2);
        to_xyb(buf[0], buf[2], stride2, w2, h2);
        to_xyb(buf[1], buf[3], stride2, w2, h2);

        for (uint32_t plane = 0; plane < 3; plane++) {
            multiply(buf[2][plane], buf[2][plane], buf[4][0], stride2, w2, h2);
            blur(buf[4][0], buf[4][1], stride2, w2, h2, scratch);
            multiply(buf[3][plane], buf[3][plane], buf[4][0], stride2, w2, h2);
            blur(buf[4][0], buf[4][2], stride2, w2, h2, scratch);
            multiply(buf[2][plane], buf[3][plane], buf[4][0], stride2, w2, h2);
            blur(buf[4][0], buf[5][0], stride2, w2, h2, scratch);
            blur(buf[2][plane], buf[5][1], stride2, w2, h2, scratch);
            blur(buf[3][plane], buf[4][0], stride2, w2, h2, scratch);

            ssim_map(buf[4][1], buf[4][2], buf[5][0], buf[5][1], buf[4][0],
                     stride2, w2, h2, plane, one_per_pixels,
                     avg_ssim[scale], error_scale, scale);
            edge_map(buf[2][plane], buf[3][plane], buf[5][1], buf[4][0],
                     stride2, w2, h2, plane, one_per_pixels,
                     avg_edge[scale], error_scale, scale);
        }

        if (error_map != NULL) {
            if (scale == 0) {
                memcpy(error_accum, error_scale, current_wh * sizeof(float));
            } else if (scale < 3) {
                upscale_and_accumulate(error_scale, error_accum, stride2, w2, h2,
                                       stride, reference->width, reference->height);
            }
        }
    }

    *result = score(avg_ssim, avg_edge);
    if (error_map != NULL)
        generate_error_map(error_accum, error_map, stride, reference->width,
                           reference->height);

    free(planes);
    free(temp);
    free(scratch);
    return FMETRICS_OK;
}

FmetricsErr fmetrics_ssimu2_cmp(const FmetricsImg *const reference,
                                const FmetricsImg *const distorted,
                                double *const result)
{
    if (result == NULL) return FMETRICS_ERR_INVALID_ARGUMENT;
    return ssimu2_compute(reference, distorted, result, NULL);
}

FmetricsErr fmetrics_ssimu2_cmp_map(const FmetricsImg *const reference,
                                    const FmetricsImg *const distorted,
                                    double *const result,
                                    uint32_t *const error_map)
{
    if (result == NULL || error_map == NULL) return FMETRICS_ERR_INVALID_ARGUMENT;
    return ssimu2_compute(reference, distorted, result, error_map);
}

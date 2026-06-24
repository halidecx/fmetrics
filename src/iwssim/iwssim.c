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
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "../common/util.h"
#include "../fmetrics.h"
#include "internal.h"

static bool image_alloc(ImageD *const im, const int width, const int height,
                        ScratchBuffer *const scratch)
{
    if (width <= 0 || height <= 0) return false;
    const size_t pixels = (size_t)width * (size_t)height;
    im->data = scratch_alloc(scratch, pixels * sizeof(*im->data));
    if (im->data == NULL) return false;
    im->width = width;
    im->height = height;
    return true;
}

static void band_dimensions(const int width, const int height,
                            int band_w[IWSSIM_NSCALES],
                            int band_h[IWSSIM_NSCALES])
{
    int w = width;
    int h = height;
    for (int s = 0; s < IWSSIM_NSCALES; s++) {
        band_w[s] = w;
        band_h[s] = h;
        if (s < IWSSIM_NSCALES - 1) {
            w = (w + 1) >> 1;
            h = (h + 1) >> 1;
        }
    }
}

static size_t reflect_index(int p, const int n) {
    if (n <= 1) return 0;
    while (p < 0 || p >= n)
        if (p < 0)
            p = -p;
        else
            p = 2 * n - p - 2;
    return (size_t)p;
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
    if (reference->width < 161 || reference->height < 161)
        return FMETRICS_ERR_IWSSIM_IMG_TOO_SMALL;
    if (reference->stride < reference->width * 3 ||
        distorted->stride < distorted->width * 3)
    {
        return FMETRICS_ERR_INVALID_ARGUMENT;
    }
    return FMETRICS_OK;
}

static bool rgb_to_luma(const FmetricsImg *const src, ImageD *const dst,
                        ScratchBuffer *const scratch)
{
    if (!image_alloc(dst, (int)src->width, (int)src->height, scratch))
        return false;
    for (size_t y = 0; y < src->height; y++) {
        const uint8_t *row = src->data + y * (size_t)src->stride;
        for (size_t x = 0; x < src->width; x++) {
            const uint8_t *px = row + x * 3u;
            dst->data[y * (size_t)dst->width + x] =
                (float)(0.299 * (double)px[0] + 0.587 *
                        (double)px[1] + 0.114 * (double)px[2]);
        }
    }
    return true;
}

static bool reduce_burt(const ImageD *const src, const ImageD *const dst,
                        ScratchBuffer *const scratch)
{
    const int out_w = (src->width + 1) >> 1;
    const int out_h = (src->height + 1) >> 1;
    const ScratchMark mark = scratch_mark(scratch);
    ImageD temp = {0};

    if (!image_alloc(&temp, out_w, src->height, scratch))
        return false;

    for (size_t y = 0; y < src->height; y++) {
        const float *restrict row = src->data + y * (size_t)src->width;
        float *restrict out = temp.data + y * (size_t)out_w;
        float sum0 = 0.0f;
        for (int kx = 0; kx < 5; kx++) {
            const int sx = (int)reflect_index(kx - 2, src->width);
            sum0 += BURT_KRNL[kx] * row[sx];
        }
        out[0] = sum0;

        for (int x = 1; x < out_w - 1; x++) {
            const int base = 2 * x - 2;
            out[x] = BURT_KRNL[0] * row[base]
                   + BURT_KRNL[1] * row[base + 1]
                   + BURT_KRNL[2] * row[base + 2]
                   + BURT_KRNL[3] * row[base + 3]
                   + BURT_KRNL[4] * row[base + 4];
        }

        if (out_w > 1) {
            float sum = 0.0f;
            for (int kx = 0; kx < 5; kx++) {
                const int sx = (int)reflect_index(2 * (out_w - 1) + kx - 2, src->width);
                sum += BURT_KRNL[kx] * row[sx];
            }
            out[out_w - 1] = sum;
        }
    }

    const float *restrict t0 = temp.data;
    const size_t stride = (size_t)out_w;

    float *restrict out = dst->data;
    for (int x = 0; x < out_w; x++) {
        float sum0 = 0.0f;
        for (int ky = 0; ky < 5; ky++) {
            const size_t sy = reflect_index(ky - 2, src->height);
            sum0 += BURT_KRNL[ky] * t0[sy * stride + (size_t)x];
        }
        out[x] = sum0;
    }

    for (size_t y = 1; y < out_h - 1; y++) {
        out = dst->data + y * (size_t)out_w;
        const size_t base = 2 * y - 2;
        const float *restrict r0 = t0 + base * stride;
        const float *restrict r1 = r0 + stride;
        const float *restrict r2 = r1 + stride;
        const float *restrict r3 = r2 + stride;
        const float *restrict r4 = r3 + stride;
        for (int x = 0; x < out_w; x++) {
            out[x] = BURT_KRNL[0] * r0[x]
                    + BURT_KRNL[1] * r1[x]
                    + BURT_KRNL[2] * r2[x]
                    + BURT_KRNL[3] * r3[x]
                    + BURT_KRNL[4] * r4[x];
        }
    }

    if (out_h > 1) {
        out = dst->data + (size_t)(out_h - 1) * (size_t)out_w;
        for (int x = 0; x < out_w; x++) {
            float sum = 0.0f;
            for (int ky = 0; ky < 5; ky++) {
                const size_t sy = reflect_index(2 * (out_h - 1) + ky - 2, src->height);
                sum += BURT_KRNL[ky] * t0[sy * stride + (size_t)x];
            }
            out[x] = sum;
        }
    }

    scratch_reset(scratch, mark);
    return true;
}

static bool expand_burt_to(const ImageD *const src, const int out_w,
                           const int out_h, const ImageD *const dst,
                           ScratchBuffer *const scratch)
{
    const ScratchMark mark = scratch_mark(scratch);
    ImageD temp = {0};
    if (!image_alloc(&temp, out_w, src->height, scratch)) return false;

    const int h_interior_end = imin(2 * src->width - 2, out_w);

    for (size_t sy = 0; sy < src->height; sy++) {
        const float *restrict row = src->data + sy * (size_t)src->width;
        float *restrict out = temp.data + sy * (size_t)out_w;

        float sum0 = 0.0f;
        for (int kx = 0; kx < 5; kx++) {
            const int ux = kx - 2;
            if ((ux & 1) != 0) continue;
            const int sx = (int)reflect_index(ux / 2, src->width);
            sum0 += BURT_KRNL[kx] * row[sx];
        }
        out[0] = sum0;

        for (int x = 1; x < h_interior_end; x++) {
            float sum = 0.0f;
            for (int kx = 0; kx < 5; kx++) {
                const int ux = x + kx - 2;
                if ((ux & 1) != 0) continue;
                sum += BURT_KRNL[kx] * row[ux / 2];
            }
            out[x] = sum;
        }

        for (int x = h_interior_end; x < out_w; x++) {
            float sum = 0.0f;
            for (int kx = 0; kx < 5; kx++) {
                const int ux = x + kx - 2;
                if ((ux & 1) != 0) continue;
                const int sx = (int)reflect_index(ux / 2, src->width);
                sum += BURT_KRNL[kx] * row[sx];
            }
            out[x] = sum;
        }
    }

    const int v_interior_end = imin(2 * src->height - 2, out_h);
    const size_t stride = (size_t)out_w;

    float *restrict out = dst->data;
    for (int x = 0; x < out_w; x++) {
        float sum = 0.0f;
        for (int ky = 0; ky < 5; ky++) {
            const int uy = ky - 2;
            if ((uy & 1) != 0) continue;
            const size_t sy = reflect_index(uy / 2, src->height);
            sum += BURT_KRNL[ky] * temp.data[sy * stride + (size_t)x];
        }
        out[x] = sum;
    }

    for (int y = 1; y < v_interior_end; y++) {
        float *restrict out = dst->data + (size_t)y * stride;
        for (int x = 0; x < out_w; x++) {
            float sum = 0.0f;
            for (int ky = 0; ky < 5; ky++) {
                const int uy = y + ky - 2;
                if ((uy & 1) != 0) continue;
                sum += BURT_KRNL[ky] * temp.data[(size_t)(uy / 2) * stride + x];
            }
            out[x] = sum;
        }
    }

    for (int y = v_interior_end; y < out_h; y++) {
        float *restrict out = dst->data + (size_t)y * stride;
        for (int x = 0; x < out_w; x++) {
            float sum = 0.0f;
            for (int ky = 0; ky < 5; ky++) {
                const int uy = y + ky - 2;
                if ((uy & 1) != 0) continue;
                const size_t sy = reflect_index(uy / 2, src->height);
                sum += BURT_KRNL[ky] * temp.data[sy * stride + (size_t)x];
            }
            out[x] = sum;
        }
    }

    scratch_reset(scratch, mark);
    return true;
}

static bool pyramid_build(const ImageD *const base, Pyramid *const pyr,
                          ScratchBuffer *const scratch)
{
    int w = base->width, h = base->height;
    for (int scale = 0; scale < IWSSIM_NSCALES; scale++) {
        if (!image_alloc(&pyr->bands[scale], w, h, scratch)) return false;
        if (scale < IWSSIM_NSCALES - 1) {
            w = (w + 1) >> 1;
            h = (h + 1) >> 1;
        }
    }

    const ScratchMark pyr_mark = scratch_mark(scratch);
    ImageD buf_a, buf_b;
    if (!image_alloc(&buf_a, base->width, base->height, scratch) ||
        !image_alloc(&buf_b, base->width, base->height, scratch))
    {
        return false;
    }

    const size_t base_pixels = (size_t)base->width * (size_t)base->height;
    memcpy(buf_a.data, base->data, base_pixels * sizeof(float));

    ImageD *cur = &buf_a;
    ImageD *next = &buf_b;

    for (int scale = 0; scale < IWSSIM_NSCALES - 1; scale++) {
        next->width = (cur->width + 1) >> 1;
        next->height = (cur->height + 1) >> 1;

        if (!reduce_burt(cur, next, scratch)) return false;

        const ScratchMark exp_mark = scratch_mark(scratch);
        ImageD expanded = {0};
        if (!image_alloc(&expanded, cur->width, cur->height, scratch))
            return false;
        if (!expand_burt_to(next, cur->width, cur->height, &expanded, scratch))
            return false;

        const size_t band_pixels = (size_t)cur->width * (size_t)cur->height;
        for (size_t i = 0; i < band_pixels; i++)
            pyr->bands[scale].data[i] = cur->data[i] - expanded.data[i];

        scratch_reset(scratch, exp_mark);

        ImageD *const tmp = cur;
        cur = next;
        next = tmp;
    }

    const size_t last_pixels = (size_t)cur->width * (size_t)cur->height;
    memcpy(pyr->bands[IWSSIM_NSCALES - 1].data, cur->data, last_pixels * sizeof(float));

    scratch_reset(scratch, pyr_mark);
    return true;
}

static void gaussian_11(float kernel[IWSSIM_WINSIZE]) {
    double sum = 0.0;
    double dk[IWSSIM_WINSIZE];

    for (int x = 0; x < IWSSIM_WINSIZE; x++) {
        const int xx = x - IWSSIM_WINSIZE / 2;
        const double v = exp(-((double)(xx * xx)) / (2.0 * SIGMA * SIGMA));
        dk[x] = v;
        sum += v;
    }
    for (int x = 0; x < IWSSIM_WINSIZE; x++)
        kernel[x] = (float)(dk[x] / sum);
}

static bool conv_valid_gaussian_product(const ImageD *const a,
                                        const ImageD *const b,
                                        const float kernel[IWSSIM_WINSIZE],
                                        const ImageD *const dst,
                                        ScratchBuffer *const scratch,
                                        const bool product)
{
    if (a->width != b->width || a->height != b->height) return false;

    const int out_w = a->width - IWSSIM_WINSIZE + 1;
    const int out_h = a->height - IWSSIM_WINSIZE + 1;
    if (out_w <= 0 || out_h <= 0) return false;

    const ScratchMark mark = scratch_mark(scratch);
    ImageD temp = {0};
    if (!image_alloc(&temp, out_w, a->height, scratch)) return false;

    for (size_t y = 0; y < (size_t)a->height; y++) {
        const float *restrict row_a = a->data + y * (size_t)a->width;
        const float *restrict row_b = b->data + y * (size_t)b->width;
        float *restrict out = temp.data + y * (size_t)out_w;
        for (int x = 0; x < out_w; x++) {
            float sum = 0.0f;
            for (int kx = 0; kx < IWSSIM_WINSIZE; kx++) {
                const float a_px = row_a[x + kx];
                sum += kernel[kx] * a_px * (product ? row_b[x + kx] : 1.0f);
            }
            out[x] = sum;
        }
    }

    for (size_t y = 0; y < (size_t)out_h; y++) {
        float *restrict out = dst->data + y * (size_t)out_w;
        for (int x = 0; x < out_w; x++) {
            float sum = 0.0f;
            for (int ky = 0; ky < IWSSIM_WINSIZE; ky++)
                sum += kernel[ky] *
                    temp.data[(y + (size_t)ky) * (size_t)out_w + (size_t)x];
            out[x] = sum;
        }
    }

    scratch_reset(scratch, mark);
    return true;
}

static bool compute_quality_maps(const Pyramid *const ref_pyr,
                                 const Pyramid *const dis_pyr,
                                 const ImageD cs_maps[IWSSIM_NSCALES],
                                 const ImageD *const l_map,
                                 ScratchBuffer *const scratch)
{
    float kernel[IWSSIM_WINSIZE];
    gaussian_11(kernel);

    for (int s = 0; s < IWSSIM_NSCALES; s++) {
        const ImageD *const ref = &ref_pyr->bands[s];
        const ImageD *const dis = &dis_pyr->bands[s];
        const int out_w = ref->width - IWSSIM_WINSIZE + 1;
        const int out_h = ref->height - IWSSIM_WINSIZE + 1;

        const ScratchMark scale_mark = scratch_mark(scratch);

        ImageD mu1 = {0}, mu2 = {0};
        ImageD conv_ref_dis = {0}, conv_ref_sq = {0}, conv_dis_sq = {0};

        if (!image_alloc(&mu1, out_w, out_h, scratch) ||
            !conv_valid_gaussian_product(ref, ref, kernel, &mu1, scratch, false) ||
            !image_alloc(&mu2, out_w, out_h, scratch) ||
            !conv_valid_gaussian_product(dis, dis, kernel, &mu2, scratch, false) ||
            !image_alloc(&conv_ref_dis, out_w, out_h, scratch) ||
            !conv_valid_gaussian_product(ref, dis, kernel, &conv_ref_dis,
                                         scratch, true) ||
            !image_alloc(&conv_ref_sq, out_w, out_h, scratch) ||
            !conv_valid_gaussian_product(ref, ref, kernel, &conv_ref_sq,
                                         scratch, true) ||
            !image_alloc(&conv_dis_sq, out_w, out_h, scratch) ||
            !conv_valid_gaussian_product(dis, dis, kernel, &conv_dis_sq,
                                         scratch, true))
        {
            return false;
        }

        const size_t pixels = (size_t)out_w * (size_t)out_h;
        for (size_t i = 0; i < pixels; i++) {
            const float sigma12 =
                conv_ref_dis.data[i] - mu1.data[i] * mu2.data[i];
            const float sigma1_sq =
                fmaxf(0.0f, conv_ref_sq.data[i] - mu1.data[i] * mu1.data[i]);
            const float sigma2_sq =
                fmaxf(0.0f, conv_dis_sq.data[i] - mu2.data[i] * mu2.data[i]);
            cs_maps[s].data[i] =
                (2.0f * sigma12 + C2) / (sigma1_sq + sigma2_sq + C2);
        }

        if (s == IWSSIM_NSCALES - 1)
            for (size_t i = 0; i < pixels; i++) {
                l_map->data[i] = (2.0f * mu1.data[i] * mu2.data[i] + C1) /
                    (mu1.data[i] * mu1.data[i] + mu2.data[i] * mu2.data[i] + C1);
            }

        scratch_reset(scratch, scale_mark);
    }

    return true;
}

static void local_stats_hrow_3(const ImageD *const ref, const ImageD *const dis,
                               const int y, float *const sx,
                               float *const sy, float *const sxy,
                               float *const sx2, float *const sy2)
{
    const int w = ref->width;
    if (y < 0 || y >= ref->height) {
        memset(sx, 0, (size_t)w * sizeof(*sx));
        memset(sy, 0, (size_t)w * sizeof(*sy));
        memset(sxy, 0, (size_t)w * sizeof(*sxy));
        memset(sx2, 0, (size_t)w * sizeof(*sx2));
        memset(sy2, 0, (size_t)w * sizeof(*sy2));
        return;
    }

    const float *restrict rr = ref->data + (size_t)y * (size_t)ref->width;
    const float *restrict dr = dis->data + (size_t)y * (size_t)dis->width;

    if (w == 1) {
        const float r = rr[0];
        const float d = dr[0];
        sx[0] = r;
        sy[0] = d;
        sxy[0] = r * d;
        sx2[0] = r * r;
        sy2[0] = d * d;
        return;
    }

    const float r0 = rr[0], r1 = rr[1];
    const float d0 = dr[0], d1 = dr[1];
    sx[0] = r0 + r1;
    sy[0] = d0 + d1;
    sxy[0] = r0 * d0 + r1 * d1;
    sx2[0] = r0 * r0 + r1 * r1;
    sy2[0] = d0 * d0 + d1 * d1;

    for (int x = 1; x < w - 1; x++) {
        const float r0 = rr[x - 1], r1 = rr[x], r2 = rr[x + 1];
        const float d0 = dr[x - 1], d1 = dr[x], d2 = dr[x + 1];
        sx[x] = r0 + r1 + r2;
        sy[x] = d0 + d1 + d2;
        sxy[x] = r0 * d0 + r1 * d1 + r2 * d2;
        sx2[x] = r0 * r0 + r1 * r1 + r2 * r2;
        sy2[x] = d0 * d0 + d1 * d1 + d2 * d2;
    }

    const int x = w - 1;
    const float r00 = rr[x - 1], r01 = rr[x];
    const float d00 = dr[x - 1], d01 = dr[x];
    sx[x] = r00 + r01;
    sy[x] = d00 + d01;
    sxy[x] = r00 * d00 + r01 * d01;
    sx2[x] = r00 * r00 + r01 * r01;
    sy2[x] = d00 * d00 + d01 * d01;

}

static bool local_stats_3x3(const ImageD *const ref, const ImageD *const dis,
                            const ImageD *const g_map,
                            const ImageD *const vv_map,
                            ScratchBuffer *const scratch)
{
    const ScratchMark mark = scratch_mark(scratch);
    const int w = ref->width;
    float *const rows = scratch_alloc(scratch, (size_t)15 * (size_t)w * sizeof(*rows));
    if (rows == NULL) return false;

    float *sx0 = rows + 0 * (size_t)w, *sy0 = rows + 1 * (size_t)w;
    float *sxy0 = rows + 2 * (size_t)w, *sx20 = rows + 3 * (size_t)w;
    float *sy20 = rows + 4 * (size_t)w;
    float *sx1 = rows + 5 * (size_t)w, *sy1 = rows + 6 * (size_t)w;
    float *sxy1 = rows + 7 * (size_t)w, *sx21 = rows + 8 * (size_t)w;
    float *sy21 = rows + 9 * (size_t)w;
    float *sx2 = rows + 10 * (size_t)w, *sy2 = rows + 11 * (size_t)w;
    float *sxy2 = rows + 12 * (size_t)w, *sx22 = rows + 13 * (size_t)w;
    float *sy22 = rows + 14 * (size_t)w;

    local_stats_hrow_3(ref, dis, -1, sx0, sy0, sxy0, sx20, sy20);
    local_stats_hrow_3(ref, dis, 0, sx1, sy1, sxy1, sx21, sy21);

    for (int y = 0; y < ref->height; y++) {
        local_stats_hrow_3(ref, dis, y + 1, sx2, sy2, sxy2, sx22, sy22);

        float *restrict gout = g_map->data + (size_t)y * (size_t)g_map->width;
        float *restrict vout = vv_map->data + (size_t)y * (size_t)vv_map->width;

        for (int x = 0; x < w; x++) {
            const float sx = sx0[x] + sx1[x] + sx2[x];
            const float sy = sy0[x] + sy1[x] + sy2[x];
            const float sxy = sxy0[x] + sxy1[x] + sxy2[x];
            const float sx2_sum = sx20[x] + sx21[x] + sx22[x];
            const float sy2_sum = sy20[x] + sy21[x] + sy22[x];

            const float mean_x = sx * INV9;
            const float mean_y = sy * INV9;
            const float cov_xy = sxy * INV9 - mean_x * mean_y;
            const float ss_x = fmaxf(0.0f, sx2_sum * INV9 - mean_x * mean_x);
            const float ss_y = fmaxf(0.0f, sy2_sum * INV9 - mean_y * mean_y);

            float g = cov_xy / (ss_x + TOL);
            float vv = fmaxf(0.0f, ss_y - g * cov_xy);
            if (ss_x < TOL) {
                g = 0.0f;
                vv = ss_y;
            }
            if (ss_y < TOL) {
                g = 0.0f;
                vv = 0.0f;
            }

            gout[x] = g;
            vout[x] = vv;
        }

        float *tmp;
        tmp = sx0; sx0 = sx1; sx1 = sx2; sx2 = tmp;
        tmp = sy0; sy0 = sy1; sy1 = sy2; sy2 = tmp;
        tmp = sxy0; sxy0 = sxy1; sxy1 = sxy2; sxy2 = tmp;
        tmp = sx20; sx20 = sx21; sx21 = sx22; sx22 = tmp;
        tmp = sy20; sy20 = sy21; sy21 = sy22; sy22 = tmp;
    }

    scratch_reset(scratch, mark);
    return true;
}

static inline void enlarge2_t1_coeff(const int src_n, const int t1_n,
                                     const int ti, Enlarge2Coeff *const coeff)
{
    const float p = ((float)ti + 0.5f) * (float)src_n / (float)t1_n - 0.5f;
    const int p0 = (int)floorf(p);
    coeff->w = p - (float)p0;
    coeff->i0 = iclip(p0, 0, src_n - 1);
    coeff->i1 = iclip(p0 + 1, 0, src_n - 1);
}

static inline float enlarge2_lerp(const float a, const float b, const float w)
{
    return a * (1.0f - w) + b * w;
}

static inline float enlarge2_sample_coeff(const float *const row,
                                          const Enlarge2Coeff *const coeff)
{
    return enlarge2_lerp(row[coeff->i0], row[coeff->i1], coeff->w);
}

static void enlarge2_vsample_row(const ImageD *const tmp, const int ti,
                                 float *const out)
{
    Enlarge2Coeff coeff;
    enlarge2_t1_coeff(tmp->height, 4 * tmp->height - 3, ti, &coeff);

    const float *restrict r0 = tmp->data + (size_t)coeff.i0 * (size_t)tmp->width;
    const float *restrict r1 = tmp->data + (size_t)coeff.i1 * (size_t)tmp->width;
    for (int x = 0; x < tmp->width; x++)
        out[x] = enlarge2_lerp(r0[x], r1[x], coeff.w);
}

static bool enlarge2_like_parent(const ImageD *const src, const int out_w,
                                 const int out_h, const ImageD *const dst,
                                 ScratchBuffer *const scratch)
{
    const ScratchMark mark = scratch_mark(scratch);
    ImageD tmp = {0};
    if (!image_alloc(&tmp, out_w, src->height, scratch)) return false;
    float *restrict row_tmp =
        scratch_alloc(scratch, (size_t)out_w * sizeof(float));
    if (row_tmp == NULL) {
        scratch_reset(scratch, mark);
        return false;
    }
    Enlarge2Coeff *restrict xcoeff =
        scratch_alloc(scratch, (size_t)out_w * sizeof(*xcoeff));
    if (xcoeff == NULL) {
        scratch_reset(scratch, mark);
        return false;
    }

    const int t1_w = 4 * src->width - 3;
    const int t2_w = 4 * src->width - 1;
    const bool has_right_extrap = 2 * (out_w - 1) == t2_w - 1;
    Enlarge2Coeff left0, left1, right0, right1;
    enlarge2_t1_coeff(src->width, t1_w, 0, &left0);
    enlarge2_t1_coeff(src->width, t1_w, 1, &left1);
    if (has_right_extrap) {
        enlarge2_t1_coeff(src->width, t1_w, t1_w - 1, &right0);
        enlarge2_t1_coeff(src->width, t1_w, t1_w - 2, &right1);
    }
    const int x_normal_end = has_right_extrap ? out_w - 1 : out_w;
    for (int x = 1; x < x_normal_end; x++)
        enlarge2_t1_coeff(src->width, t1_w, 2 * x - 1, &xcoeff[x]);

    for (int y = 0; y < src->height; y++) {
        const float *restrict row = src->data + (size_t)y * (size_t)src->width;
        float *restrict out = tmp.data + (size_t)y * (size_t)out_w;
        out[0] = 2.0f * enlarge2_sample_coeff(row, &left0) -
                 enlarge2_sample_coeff(row, &left1);
        for (int x = 1; x < x_normal_end; x++)
            out[x] = enlarge2_sample_coeff(row, &xcoeff[x]);
        if (has_right_extrap) {
            out[out_w - 1] = 2.0f * enlarge2_sample_coeff(row, &right0) -
                             enlarge2_sample_coeff(row, &right1);
        }
    }

    const int t1_h = 4 * src->height - 3;
    const int t2_h = 4 * src->height - 1;
    for (int y = 0; y < out_h; y++) {
        const int ty = 2 * y;
        float *restrict out = dst->data + (size_t)y * (size_t)out_w;
        if (ty == 0) {
            enlarge2_vsample_row(&tmp, 0, out);
            enlarge2_vsample_row(&tmp, 1, row_tmp);
            for (int x = 0; x < out_w; x++)
                out[x] = 2.0f * out[x] - row_tmp[x];
        } else if (ty == t2_h - 1) {
            enlarge2_vsample_row(&tmp, t1_h - 1, out);
            enlarge2_vsample_row(&tmp, t1_h - 2, row_tmp);
            for (int x = 0; x < out_w; x++)
                out[x] = 2.0f * out[x] - row_tmp[x];
        } else {
            enlarge2_vsample_row(&tmp, ty - 1, out);
        }
    }

    scratch_reset(scratch, mark);
    return true;
}

static void jacobi_eigen_10(double a[IWSSIM_MAX_NEIGHBORS][IWSSIM_MAX_NEIGHBORS],
                            const int n, double eigvals[IWSSIM_MAX_NEIGHBORS],
                            double eigvecs[IWSSIM_MAX_NEIGHBORS][IWSSIM_MAX_NEIGHBORS])
{
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++) eigvecs[r][c] = (r == c) ? 1.0 : 0.0;

    for (int iter = 0; iter < 80; iter++) {
        int p = 0, q = 1;
        double max_off = 0.0;
        for (int r = 0; r < n; r++)
            for (int c = r + 1; c < n; c++) {
                const double v = fabs(a[r][c]);
                if (v > max_off) {
                    max_off = v;
                    p = r;
                    q = c;
                }
            }
        if (max_off < 1e-12) break;

        const double app = a[p][p];
        const double aqq = a[q][q];
        const double apq = a[p][q];
        const double tau = (aqq - app) / (2.0 * apq);
        const double t =
            copysign(1.0 / (fabs(tau) + sqrt(1.0 + tau * tau)), tau);
        const double c = 1.0 / sqrt(1.0 + t * t);
        const double s = t * c;

        for (int k = 0; k < n; k++) {
            if (k == p || k == q) continue;
            const double akp = a[k][p];
            const double akq = a[k][q];
            a[k][p] = c * akp - s * akq;
            a[p][k] = a[k][p];
            a[k][q] = s * akp + c * akq;
            a[q][k] = a[k][q];
        }

        a[p][p] = c * c * app - 2.0 * s * c * apq + s * s * aqq;
        a[q][q] = s * s * app + 2.0 * s * c * apq + c * c * aqq;
        a[p][q] = 0.0;
        a[q][p] = 0.0;

        for (int k = 0; k < n; k++) {
            const double vkp = eigvecs[k][p];
            const double vkq = eigvecs[k][q];
            eigvecs[k][p] = c * vkp - s * vkq;
            eigvecs[k][q] = s * vkp + c * vkq;
        }
    }

    for (int i = 0; i < n; i++) eigvals[i] = a[i][i];
}

static inline void cov_add_9(double cov[IWSSIM_MAX_NEIGHBORS][IWSSIM_MAX_NEIGHBORS],
                             const double v0, const double v1, const double v2,
                             const double v3, const double v4, const double v5,
                             const double v6, const double v7, const double v8)
{
    double *restrict c0 = cov[0], *restrict c1 = cov[1], *restrict c2 = cov[2];
    double *restrict c3 = cov[3], *restrict c4 = cov[4], *restrict c5 = cov[5];
    double *restrict c6 = cov[6], *restrict c7 = cov[7], *restrict c8 = cov[8];

    c0[0] += v0 * v0; c0[1] += v0 * v1; c0[2] += v0 * v2;
    c0[3] += v0 * v3; c0[4] += v0 * v4; c0[5] += v0 * v5;
    c0[6] += v0 * v6; c0[7] += v0 * v7; c0[8] += v0 * v8;
    c1[1] += v1 * v1; c1[2] += v1 * v2; c1[3] += v1 * v3;
    c1[4] += v1 * v4; c1[5] += v1 * v5; c1[6] += v1 * v6;
    c1[7] += v1 * v7; c1[8] += v1 * v8;
    c2[2] += v2 * v2; c2[3] += v2 * v3; c2[4] += v2 * v4;
    c2[5] += v2 * v5; c2[6] += v2 * v6; c2[7] += v2 * v7;
    c2[8] += v2 * v8;
    c3[3] += v3 * v3; c3[4] += v3 * v4; c3[5] += v3 * v5;
    c3[6] += v3 * v6; c3[7] += v3 * v7; c3[8] += v3 * v8;
    c4[4] += v4 * v4; c4[5] += v4 * v5; c4[6] += v4 * v6;
    c4[7] += v4 * v7; c4[8] += v4 * v8;
    c5[5] += v5 * v5; c5[6] += v5 * v6; c5[7] += v5 * v7;
    c5[8] += v5 * v8;
    c6[6] += v6 * v6; c6[7] += v6 * v7; c6[8] += v6 * v8;
    c7[7] += v7 * v7; c7[8] += v7 * v8;
    c8[8] += v8 * v8;
}

static inline void cov_add_10(double cov[IWSSIM_MAX_NEIGHBORS][IWSSIM_MAX_NEIGHBORS],
                              const double v0, const double v1, const double v2,
                              const double v3, const double v4, const double v5,
                              const double v6, const double v7, const double v8,
                              const double v9)
{
    cov_add_9(cov, v0, v1, v2, v3, v4, v5, v6, v7, v8);
    cov[0][9] += v0 * v9;
    cov[1][9] += v1 * v9;
    cov[2][9] += v2 * v9;
    cov[3][9] += v3 * v9;
    cov[4][9] += v4 * v9;
    cov[5][9] += v5 * v9;
    cov[6][9] += v6 * v9;
    cov[7][9] += v7 * v9;
    cov[8][9] += v8 * v9;
    cov[9][9] += v9 * v9;
}

static bool compute_iw_map_for_scale(const Pyramid *const ref_pyr,
                                     const Pyramid *const dis_pyr,
                                     const int scale,
                                     const ImageD *const iw_map,
                                     ScratchBuffer *const scratch)
{
    const ImageD *const ref = &ref_pyr->bands[scale];
    const ImageD *const dis = &dis_pyr->bands[scale];
    const bool use_parent = scale < IWSSIM_NSCALES - 2;
    const int n_features = 9 + (int)use_parent;
    const int out_w = ref->width - 2;
    const int out_h = ref->height - 2;

    ScratchMark scale_mark = scratch_mark(scratch);

    ImageD g_map = {0}, vv_map = {0}, ss_map = {0}, parent = {0};
    if (!image_alloc(&g_map, ref->width, ref->height, scratch) ||
        !image_alloc(&vv_map, ref->width, ref->height, scratch) ||
        !image_alloc(&ss_map, out_w, out_h, scratch))
    {
        return false;
    }
    if (!local_stats_3x3(ref, dis, &g_map, &vv_map, scratch))
        return false;

    if (use_parent) {
        if (!image_alloc(&parent, ref->width, ref->height, scratch))
            return false;
        if (!enlarge2_like_parent(&ref_pyr->bands[scale + 1], ref->width,
                                  ref->height, &parent, scratch))
        {
            return false;
        }
    }

    double cov[IWSSIM_MAX_NEIGHBORS][IWSSIM_MAX_NEIGHBORS] = {{0.0}};
    const double nexp = (double)out_w * (double)out_h;
    const size_t bw = (size_t)ref->width;

    if (use_parent) {
        for (size_t y = 1; y < ref->height - 1; y++) {
            const float *restrict r0 = ref->data + (y - 1) * bw;
            const float *restrict r1 = ref->data + y * bw;
            const float *restrict r2 = ref->data + (y + 1) * bw;
            const float *restrict prow = parent.data + y * (size_t)parent.width;

            for (int x = 1; x < ref->width - 1; x++) {
                const double v0 = (double)r0[x - 1];
                const double v1 = (double)r0[x];
                const double v2 = (double)r0[x + 1];
                const double v3 = (double)r1[x - 1];
                const double v4 = (double)r1[x];
                const double v5 = (double)r1[x + 1];
                const double v6 = (double)r2[x - 1];
                const double v7 = (double)r2[x];
                const double v8 = (double)r2[x + 1];
                cov_add_10(cov, v0, v1, v2, v3, v4, v5, v6, v7, v8,
                           (double)prow[x]);
            }
        }
    } else {
        for (size_t y = 1; y < ref->height - 1; y++) {
            const float *restrict r0 = ref->data + (y - 1) * bw;
            const float *restrict r1 = ref->data + y * bw;
            const float *restrict r2 = ref->data + (y + 1) * bw;

            for (int x = 1; x < ref->width - 1; x++) {
                const double v0 = (double)r0[x - 1];
                const double v1 = (double)r0[x];
                const double v2 = (double)r0[x + 1];
                const double v3 = (double)r1[x - 1];
                const double v4 = (double)r1[x];
                const double v5 = (double)r1[x + 1];
                const double v6 = (double)r2[x - 1];
                const double v7 = (double)r2[x];
                const double v8 = (double)r2[x + 1];
                cov_add_9(cov, v0, v1, v2, v3, v4, v5, v6, v7, v8);
            }
        }
    }
    for (int r = 0; r < n_features; r++)
        for (int c = r; c < n_features; c++) {
            cov[r][c] /= nexp;
            cov[c][r] = cov[r][c];
        }

    double eigvals[IWSSIM_MAX_NEIGHBORS] = {0.0};
    double eigvecs[IWSSIM_MAX_NEIGHBORS][IWSSIM_MAX_NEIGHBORS] = {{0.0}};
    jacobi_eigen_10(cov, n_features, eigvals, eigvecs);

    const double ev[IWSSIM_MAX_NEIGHBORS] = {
        fmax(0.0, eigvals[0]), fmax(0.0, eigvals[1]),
        fmax(0.0, eigvals[2]), fmax(0.0, eigvals[3]),
        fmax(0.0, eigvals[4]), fmax(0.0, eigvals[5]),
        fmax(0.0, eigvals[6]), fmax(0.0, eigvals[7]),
        fmax(0.0, eigvals[8]), fmax(0.0, eigvals[9]),
    };

    double sum_all = 0.0, sum_positive = 0.0;
    for (int i = 0; i < n_features; i++) {
        sum_all += eigvals[i];
        if (eigvals[i] > 0.0) sum_positive += eigvals[i];
    }
    const double eig_scale =
        (sum_positive > 0.0) ? (sum_all / sum_positive) : 0.0;

    double inv_lambda[IWSSIM_MAX_NEIGHBORS] = {0.0};
    for (int col = 0; col < n_features; col++) {
        const double lambda =
            eigvals[col] > 0.0 ? eigvals[col] * eig_scale : 0.0;
        inv_lambda[col] = lambda > 1e-12 ? 1.0 / lambda : 0.0;
    }

    double ss_mat[IWSSIM_MAX_NEIGHBORS][IWSSIM_MAX_NEIGHBORS] = {{0.0}};
    for (int r = 0; r < n_features; r++)
        for (int c = r; c < n_features; c++) {
            double sum = 0.0;
            for (int col = 0; col < n_features; col++)
                sum += eigvecs[r][col] * eigvecs[c][col] * inv_lambda[col];
            ss_mat[r][c] = sum;
            ss_mat[c][r] = sum;
        }

    for (size_t y = 1; y < ref->height - 1; y++) {
        const float *restrict r0 = ref->data + (y - 1) * bw;
        const float *restrict r1 = ref->data + y * bw;
        const float *restrict r2 = ref->data + (y + 1) * bw;
        const float *restrict prow =
            use_parent ? parent.data + y * (size_t)parent.width : NULL;
        float *restrict out = ss_map.data + (y - 1) * (size_t)out_w;

        for (int x = 1; x < ref->width - 1; x++) {
            const double v0 = (double)r0[x - 1];
            const double v1 = (double)r0[x];
            const double v2 = (double)r0[x + 1];
            const double v3 = (double)r1[x - 1];
            const double v4 = (double)r1[x];
            const double v5 = (double)r1[x + 1];
            const double v6 = (double)r2[x - 1];
            const double v7 = (double)r2[x];
            const double v8 = (double)r2[x + 1];
            const double v9 = use_parent ? (double)prow[x] : 0.0;

            double ss =
                ss_mat[0][0] * v0 * v0 + ss_mat[1][1] * v1 * v1
              + ss_mat[2][2] * v2 * v2 + ss_mat[3][3] * v3 * v3
              + ss_mat[4][4] * v4 * v4 + ss_mat[5][5] * v5 * v5
              + ss_mat[6][6] * v6 * v6 + ss_mat[7][7] * v7 * v7
              + ss_mat[8][8] * v8 * v8
              + 2.0 * (
                    ss_mat[0][1] * v0 * v1 + ss_mat[0][2] * v0 * v2
                  + ss_mat[0][3] * v0 * v3 + ss_mat[0][4] * v0 * v4
                  + ss_mat[0][5] * v0 * v5 + ss_mat[0][6] * v0 * v6
                  + ss_mat[0][7] * v0 * v7 + ss_mat[0][8] * v0 * v8
                  + ss_mat[1][2] * v1 * v2 + ss_mat[1][3] * v1 * v3
                  + ss_mat[1][4] * v1 * v4 + ss_mat[1][5] * v1 * v5
                  + ss_mat[1][6] * v1 * v6 + ss_mat[1][7] * v1 * v7
                  + ss_mat[1][8] * v1 * v8 + ss_mat[2][3] * v2 * v3
                  + ss_mat[2][4] * v2 * v4 + ss_mat[2][5] * v2 * v5
                  + ss_mat[2][6] * v2 * v6 + ss_mat[2][7] * v2 * v7
                  + ss_mat[2][8] * v2 * v8 + ss_mat[3][4] * v3 * v4
                  + ss_mat[3][5] * v3 * v5 + ss_mat[3][6] * v3 * v6
                  + ss_mat[3][7] * v3 * v7 + ss_mat[3][8] * v3 * v8
                  + ss_mat[4][5] * v4 * v5 + ss_mat[4][6] * v4 * v6
                  + ss_mat[4][7] * v4 * v7 + ss_mat[4][8] * v4 * v8
                  + ss_mat[5][6] * v5 * v6 + ss_mat[5][7] * v5 * v7
                  + ss_mat[5][8] * v5 * v8 + ss_mat[6][7] * v6 * v7
                  + ss_mat[6][8] * v6 * v8 + ss_mat[7][8] * v7 * v8);
            if (use_parent) {
                ss += ss_mat[9][9] * v9 * v9
                    + 2.0 * (
                          ss_mat[0][9] * v0 * v9 + ss_mat[1][9] * v1 * v9
                        + ss_mat[2][9] * v2 * v9 + ss_mat[3][9] * v3 * v9
                        + ss_mat[4][9] * v4 * v9 + ss_mat[5][9] * v5 * v9
                        + ss_mat[6][9] * v6 * v9 + ss_mat[7][9] * v7 * v9
                        + ss_mat[8][9] * v8 * v9);
            }
            out[x - 1] = (float)(ss / (double)n_features);
        }
    }

    for (size_t y = 0; y < out_h; y++) {
        const size_t row_i = (y + 1) * (size_t)ref->width + 1;
        const size_t dst_row = y * (size_t)out_w;
        for (int x = 0; x < out_w; x++) {
            const size_t src_i = row_i + (size_t)x;
            const size_t dst_i = dst_row + (size_t)x;
            const double g = (double)g_map.data[src_i];
            const double vv = (double)vv_map.data[src_i];
            const double ss = (double)ss_map.data[dst_i];

            const double A = (vv + (1.0 + g * g) * SIGMA_NSQ) * ss * INV_SIGMA_NSQ_SQ;
            const double D = vv / SIGMA_NSQ;
            const double base = 1.0 + D;

            double product =
                (base + A * ev[0]) * (base + A * ev[1]) *
                (base + A * ev[2]) * (base + A * ev[3]) *
                (base + A * ev[4]) * (base + A * ev[5]) *
                (base + A * ev[6]) * (base + A * ev[7]) *
                (base + A * ev[8]);
            if (use_parent)
                product *= base + A * ev[9];
            const float infow = product > 0.0 ? flog2(product) : 0.0f;
            iw_map->data[dst_i] = infow < TOL ? 0.0f : infow;
        }
    }

    scratch_reset(scratch, scale_mark);
    return true;
}

static bool compute_iw_maps(const Pyramid *const ref_pyr,
                            const Pyramid *const dis_pyr,
                            const ImageD iw_maps[IWSSIM_NSCALES - 1],
                            ScratchBuffer *const scratch)
{
    for (int s = 0; s < IWSSIM_NSCALES - 1; s++)
        if (!compute_iw_map_for_scale(ref_pyr, dis_pyr, s, &iw_maps[s],
                                      scratch))
        {
            return false;
        }
    return true;
}

static double mean_image(const ImageD *const im) {
    const size_t pixels = (size_t)im->width * (size_t)im->height;
    double sum = 0.0;
    for (size_t i = 0; i < pixels; i++) sum += (double)im->data[i];
    return sum / (double)pixels;
}

static bool combine_maps(const ImageD cs_maps[IWSSIM_NSCALES],
                         const ImageD *const l_map,
                         const ImageD iw_maps[IWSSIM_NSCALES - 1],
                         double *const out_score)
{
    double wmcs[IWSSIM_NSCALES] = {0.0};

    for (int s = 0; s < IWSSIM_NSCALES; s++) {
        const ImageD *cs = &cs_maps[s];
        if (s == IWSSIM_NSCALES - 1) {
            if (cs->width != l_map->width || cs->height != l_map->height)
                return false;
            const size_t pixels = (size_t)cs->width * (size_t)cs->height;
            double sum = 0.0;
            for (size_t i = 0; i < pixels; i++)
                sum += (double)cs->data[i] * (double)l_map->data[i];
            wmcs[s] = sum / (double)pixels;
            continue;
        }

        const ImageD *const iw = &iw_maps[s];
        const int crop = 4;
        if (iw->width - 2 * crop != cs->width ||
            iw->height - 2 * crop != cs->height)
        {
            return false;
        }

        double weighted_sum = 0.0, weight_sum = 0.0;
        for (int y = 0; y < cs->height; y++)
            for (int x = 0; x < cs->width; x++) {
                const double c = (double)cs->data[(size_t)y * (size_t)cs->width + (size_t)x];
                const double w = (double)iw->data[(size_t)(y + crop) * (size_t)iw->width + (size_t)(x + crop)];
                weighted_sum += c * w;
                weight_sum += w;
            }
        wmcs[s] = weight_sum > 0.0 ? weighted_sum / weight_sum : mean_image(cs);
    }

    double score = 1.0;
    for (int s = 0; s < IWSSIM_NSCALES; s++)
        score *= pow(fabs(wmcs[s]), MS_SSIM_WEIGHTS[s]);
    *out_score = score;
    return true;
}

static size_t iwssim_scratch_size(const uint32_t width,
                                  const uint32_t height)
{
    int band_w[IWSSIM_NSCALES], band_h[IWSSIM_NSCALES];
    band_dimensions((int)width, (int)height, band_w, band_h);

    size_t persistent = 2 * (size_t)band_w[0] * band_h[0];

    for (int s = 0; s < IWSSIM_NSCALES; s++)
        persistent += 2 * (size_t)band_w[s] * band_h[s];

    for (int s = 0; s < IWSSIM_NSCALES; s++) {
        const int cw = band_w[s] - IWSSIM_WINSIZE + 1;
        const int ch = band_h[s] - IWSSIM_WINSIZE + 1;
        if (cw > 0 && ch > 0)
            persistent += (size_t)cw * ch;
    }

    const int cw = band_w[IWSSIM_NSCALES - 1] - IWSSIM_WINSIZE + 1;
    const int ch = band_h[IWSSIM_NSCALES - 1] - IWSSIM_WINSIZE + 1;
    if (cw > 0 && ch > 0)
        persistent += (size_t)cw * ch;

    for (int s = 0; s < IWSSIM_NSCALES - 1; s++) {
        const int iw = band_w[s] - 2;
        const int ih = band_h[s] - 2;
        if (iw > 0 && ih > 0)
            persistent += (size_t)iw * ih;
    }

    /* Temp peak: worst of pyramid_build, compute_quality_maps, compute_iw_maps
     * pyramid_build: 2*W*H (ping-pong) + W*H (expanded) + W*(H+1)/2 (expand temp) ≈ 3.5*W*H
     * compute_quality_maps: 5*(W-10)*(H-10) + (W-10)*H ≈ 6*W*H
     * compute_iw_maps: 3*W*H + W*H (parent) ≈ 4*W*H */
    const size_t wh = (size_t)band_w[0] * band_h[0];
    const size_t temp_peak = 7 * wh; // generous upper bound

    const size_t total_doubles = persistent + temp_peak;
    const size_t total_bytes = total_doubles * sizeof(float);
    return total_bytes + 4096; // alignment padding
}

static FmetricsErr iwssim_score_luma(const ImageD *const reference,
                                     const ImageD *const distorted,
                                     double *const out_score,
                                     ScratchBuffer *const scratch)
{
    Pyramid ref_pyr = {{{0}}}, dis_pyr = {{{0}}};
    ImageD cs_maps[IWSSIM_NSCALES] = {0};
    ImageD iw_maps[IWSSIM_NSCALES - 1] = {0};
    ImageD l_map = {0};

    int band_w[IWSSIM_NSCALES], band_h[IWSSIM_NSCALES];
    band_dimensions(reference->width, reference->height, band_w, band_h);

    for (int s = 0; s < IWSSIM_NSCALES; s++) {
        const int cw = band_w[s] - IWSSIM_WINSIZE + 1;
        const int ch = band_h[s] - IWSSIM_WINSIZE + 1;
        if (!image_alloc(&cs_maps[s], cw, ch, scratch))
            return FMETRICS_ERR_OUT_OF_MEMORY;
    }

    const int cw = band_w[IWSSIM_NSCALES - 1] - IWSSIM_WINSIZE + 1;
    const int ch = band_h[IWSSIM_NSCALES - 1] - IWSSIM_WINSIZE + 1;
    if (!image_alloc(&l_map, cw, ch, scratch))
        return FMETRICS_ERR_OUT_OF_MEMORY;

    for (int s = 0; s < IWSSIM_NSCALES - 1; s++) {
        const int iw_w = band_w[s] - 2;
        const int iw_h = band_h[s] - 2;
        if (!image_alloc(&iw_maps[s], iw_w, iw_h, scratch))
            return FMETRICS_ERR_OUT_OF_MEMORY;
    }

    if (!pyramid_build(reference, &ref_pyr, scratch) ||
        !pyramid_build(distorted, &dis_pyr, scratch))
    {
        return FMETRICS_ERR_OUT_OF_MEMORY;
    }

    for (int s = 0; s < IWSSIM_NSCALES; s++)
        if (ref_pyr.bands[s].width < IWSSIM_WINSIZE ||
            ref_pyr.bands[s].height < IWSSIM_WINSIZE)
        {
            return FMETRICS_ERR_IWSSIM_IMG_TOO_SMALL;
        }

    if (!compute_quality_maps(&ref_pyr, &dis_pyr, cs_maps, &l_map, scratch) ||
        !compute_iw_maps(&ref_pyr, &dis_pyr, iw_maps, scratch) ||
        !combine_maps(cs_maps, &l_map, iw_maps, out_score))
    {
        return FMETRICS_ERR_OUT_OF_MEMORY;
    }

    return FMETRICS_OK;
}

FmetricsErr fmetrics_iwssim_cmp(const FmetricsImg *const reference,
                                const FmetricsImg *const distorted,
                                double *const result)
{
    if (result == NULL) return FMETRICS_ERR_INVALID_ARGUMENT;
    const FmetricsErr valid = validate_image_pair(reference, distorted);
    if (valid != FMETRICS_OK) return valid;

    const size_t scratch_size =
        iwssim_scratch_size(reference->width, reference->height);
    ScratchBuffer scratch;
    scratch.data = malloc(scratch_size);
    if (scratch.data == NULL) return FMETRICS_ERR_OUT_OF_MEMORY;
    scratch.size = scratch_size;
    scratch.offset = 0;

    ImageD ref_luma = {0}, dis_luma = {0};
    FmetricsErr err = FMETRICS_OK;

    if (!rgb_to_luma(reference, &ref_luma, &scratch) ||
        !rgb_to_luma(distorted, &dis_luma, &scratch)) {
        err = FMETRICS_ERR_OUT_OF_MEMORY;
        free(scratch.data);
        return err;
    }

    double score = 0.0;
    err = iwssim_score_luma(&ref_luma, &dis_luma, &score, &scratch);
    if (err != FMETRICS_OK) {
        free(scratch.data);
        return err;
    }

    *result = fclip(score, 0.0, 1.0);

    free(scratch.data);
    return err;
}

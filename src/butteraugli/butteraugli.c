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
#include "../common/color.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/util.h"
#include "../fmetrics.h"
#include "internal.h"

static bool img_alloc(ImageF *im, const size_t x, const size_t y,
                      ScratchBuffer *s, const bool zero)
{
    im->x = x;
    im->y = y;
    im->p = scratch_alloc(s, x * y * sizeof(float));
    if (im->p == NULL) return false;
    if (zero) memset(im->p, 0, x * y * sizeof(float));
    return true;
}

static bool img3_alloc(Image3F *im, const size_t x, const size_t y,
                       ScratchBuffer *s, const bool zero)
{
    for (int c = 0; c < 3; c++)
        if (!img_alloc(&im->c[c], x, y, s, zero)) return false;
    return true;
}

static float *row(ImageF *im, const size_t y) {
    return im->p + y * im->x;
}

static const float *crow(const ImageF *im, const size_t y) {
    return im->p + y * im->x;
}

static const BlurKernel *blur_kernel(const float sigma) {
    const size_t n = sizeof(BLUR_KERNELS) / sizeof(BLUR_KERNELS[0]);
    for (size_t i = 0; i < n; i++)
        if (sigma == BLUR_KERNELS[i].sigma) return &BLUR_KERNELS[i];
    return NULL;
}

static size_t mirror_idx(int x, const size_t n) {
    while (x < 0 || x >= (int)n) {
        if (x < 0) x = -x - 1;
        else x = 2 * (int)n - 1 - x;
    }
    return (size_t)x;
}

static float fmaxclamp(float v, const double m) {
    static const double k = 0.724216145665;
    if (v >= m) return (float)((v - m) * k + m);
    if (v < -m) return (float)((v + m) * k - m);
    return v;
}

static float remove_range(const double w, const float x) {
    if (x > w) return (float)(x - w);
    if (x < -w) return (float)(x + w);
    return 0.0f;
}

static float amplify_range(const double w, const float x) {
    if (x > w) return (float)(x + w);
    if (x < -w) return (float)(x - w);
    return 2.0f * x;
}

static bool blur_fixed(const ImageF *const in, const BlurKernel *const g,
                       ImageF *const out, ScratchBuffer *const s)
{
    const ScratchMark mark = scratch_mark(s);
    const size_t width = in->x, height = in->y;
    const size_t diff = (size_t)g->diff;
    ImageF tmp = {0};
    if (!img_alloc(&tmp, width, height, s, false)) return false;
    for (size_t y = 0; y < height; y++) {
        const float *r = crow(in, y);
        float *t = row(&tmp, y);
        if (width > 2 * diff) {
            for (size_t x = diff; x + diff < width; x++)
                t[x] = r[x - diff] * g->k[0];
            for (int j = 1; j < g->len; j++)
                for (size_t x = diff; x + diff < width; x++)
                    t[x] += r[x - diff + (size_t)j] * g->k[j];
            for (size_t x = diff; x + diff < width; x++) t[x] *= g->scale;
        }
        const size_t left = width > 2 * diff ? diff : width;
        const size_t right = width > 2 * diff ? width - diff : width;
        for (size_t x = 0; x < left; x++) {
            const int minx = x < diff ? 0 : (int)x - g->diff;
            const int maxx = fmin((int)width - 1, (int)x + g->diff);
            float sum = 0.0f, w = 0.0f;
            for (int j = minx; j <= maxx; j++) {
                sum += r[j] * g->k[j - (int)x + g->diff];
                w += g->k[j - (int)x + g->diff];
            }
            t[x] = sum / w;
        }
        for (size_t x = right; x < width; x++) {
            const int minx = (int)x - g->diff;
            const int maxx = fmin((int)width - 1, (int)x + g->diff);
            float sum = 0.0f, w = 0.0f;
            for (int j = minx; j <= maxx; j++) {
                sum += r[j] * g->k[j - (int)x + g->diff];
                w += g->k[j - (int)x + g->diff];
            }
            t[x] = sum / w;
        }
    }
    for (size_t y = 0; y < height; y++) {
        float *o = row(out, y);
        if (y >= diff && y + diff < height) {
            const float *t = crow(&tmp, y - diff);
            for (size_t x = 0; x < width; x++) o[x] = t[x] * g->k[0];
            for (int j = 1; j < g->len; j++) {
                const float *t = crow(&tmp, y - diff + (size_t)j);
                for (size_t x = 0; x < width; x++)
                    o[x] += t[x] * g->k[j];
            }
            for (size_t x = 0; x < width; x++) o[x] *= g->scale;
        } else {
            const int miny = y < diff ? 0 : (int)y - g->diff;
            const int maxy = fmin((int)height - 1, (int)y + g->diff);
            for (size_t x = 0; x < width; x++) {
                float sum = 0.0f, w = 0.0f;
                for (int j = miny; j <= maxy; j++) {
                    sum += tmp.p[(size_t)j * width + x] *
                           g->k[j - (int)y + g->diff];
                    w += g->k[j - (int)y + g->diff];
                }
                o[x] = sum / w;
            }
        }
    }
    scratch_reset(s, mark);
    return true;
}

static bool blur(const ImageF *in, const float sigma, ImageF *out,
                 ScratchBuffer *s)
{
    const ScratchMark mark = scratch_mark(s);
    const float m = 2.25f;
    const BlurKernel *const g = blur_kernel(sigma);
    float klocal[64], scale;
    const float *k;
    int diff, len;
    if (g != NULL) {
        diff = g->diff;
        len = g->len;
        scale = g->scale;
        k = g->k;
    } else {
        diff = fmaxf(1.0f, m * fabsf(sigma));
        len = 2 * diff + 1;
        const double scaler = -1.0 / (2.0 * sigma * sigma);
        double wsum = 0.0;
        for (int i = -diff; i <= diff; i++) {
            klocal[i + diff] = (float)exp(scaler * i * i);
            wsum += klocal[i + diff];
        }
        scale = (float)(1.0 / wsum);
        k = klocal;
    }
    if (len == 5 && in != out) {
        ImageF tmp2 = {0};
        if (!img_alloc(&tmp2, in->x, in->y, s, false)) return false;
        for (size_t y = 0; y < in->y; y++) {
            const float *r = crow(in, y);
            for (size_t x = 0; x < in->x; x++) {
                if (x >= 2 && x + 2 < in->x) {
                    tmp2.p[y * in->x + x] =
                        (r[x - 2] * k[0] + r[x - 1] * k[1] +
                         r[x] * k[2] + r[x + 1] * k[3] +
                         r[x + 2] * k[4]) * scale;
                } else {
                    tmp2.p[y * in->x + x] =
                        (r[mirror_idx((int)x - 2, in->x)] * k[0] +
                         r[mirror_idx((int)x - 1, in->x)] * k[1] +
                         r[x] * k[2] +
                         r[mirror_idx((int)x + 1, in->x)] * k[3] +
                         r[mirror_idx((int)x + 2, in->x)] * k[4]) * scale;
                }
            }
        }
        for (size_t y = 0; y < in->y; y++) {
            for (size_t x = 0; x < in->x; x++) {
                if (y >= 2 && y + 2 < in->y) {
                    out->p[y * in->x + x] =
                        (tmp2.p[(y - 2) * in->x + x] * k[0] +
                         tmp2.p[(y - 1) * in->x + x] * k[1] +
                         tmp2.p[y * in->x + x] * k[2] +
                         tmp2.p[(y + 1) * in->x + x] * k[3] +
                         tmp2.p[(y + 2) * in->x + x] * k[4]) * scale;
                } else {
                    out->p[y * in->x + x] =
                        (tmp2.p[mirror_idx((int)y - 2, in->y) * in->x + x]
                         * k[0] +
                         tmp2.p[mirror_idx((int)y - 1, in->y) * in->x + x]
                         * k[1] +
                         tmp2.p[y * in->x + x] * k[2] +
                         tmp2.p[mirror_idx((int)y + 1, in->y) * in->x + x]
                         * k[3] +
                         tmp2.p[mirror_idx((int)y + 2, in->y) * in->x + x]
                         * k[4]) * scale;
                }
            }
        }
        scratch_reset(s, mark);
        return true;
    }
    if (g != NULL && in->x > 4 * (size_t)g->diff &&
        in->y > 4 * (size_t)g->diff)
        return blur_fixed(in, g, out, s);
    ImageF tmp = {0};
    if (!img_alloc(&tmp, in->y, in->x, s, false)) return false;
    for (size_t y = 0; y < in->y; y++) {
        const float *r = crow(in, y);
        for (size_t x = 0; x < in->x; x++) {
            float sum = 0.0f;
            if (x >= (size_t)diff && x + (size_t)diff < in->x) {
                const size_t d = x - (size_t)diff;
                for (int j = 0; j < len; j++)
                    sum += r[d + (size_t)j] * k[j];
                sum *= scale;
                row(&tmp, x)[y] = sum;
            } else {
                const int minx = x < (size_t)diff ? 0 : (int)x - diff;
                const int maxx = fmin((int)in->x - 1, (int)x + diff);
                float w = 0.0f;
                for (int j = minx; j <= maxx; j++) {
                    sum += r[j] * k[j - (int)x + diff];
                    w += k[j - (int)x + diff];
                }
                row(&tmp, x)[y] = sum / w;
            }
        }
    }
    for (size_t y = 0; y < tmp.y; y++) {
        const float *r = crow(&tmp, y);
        for (size_t x = 0; x < tmp.x; x++) {
            if (x >= (size_t)diff && x + (size_t)diff < tmp.x) {
                float sum = 0.0f;
                for (int j = -diff; j <= diff; j++)
                    sum += r[(int)x + j] * k[j + diff];
                row(out, x)[y] = sum * scale;
            } else {
                const int minx = x < (size_t)diff ? 0 : (int)x - diff;
                const int maxx = fmin((int)tmp.x - 1, (int)x + diff);
                float sum = 0.0f, w = 0.0f;
                for (int j = minx; j <= maxx; j++) {
                    sum += r[j] * k[j - (int)x + diff];
                    w += k[j - (int)x + diff];
                }
                row(out, x)[y] = sum / w;
            }
        }
    }
    scratch_reset(s, mark);
    return true;
}

static float diff_pre_val(const float v) {
    const float mul = 6.19424080439f, bias = mul * 12.61050594197f;
    return sqrtf(mul * fabsf(v) + bias) - 8.83812822207f;
}

static void sub(const ImageF *restrict a, const ImageF *restrict b,
                ImageF *restrict c)
{
    const size_t n = a->x * a->y;
    for (size_t i = 0; i < n; i++) c->p[i] = a->p[i] - b->p[i];
}

static void xyb_lf_to_vals(Image3F *xyb) {
    const size_t n = xyb->c[0].x * xyb->c[0].y;
    for (size_t i = 0; i < n; i++) {
        const float x = xyb->c[0].p[i];
        const float y = xyb->c[1].p[i];
        const float b = xyb->c[2].p[i] - 0.362267051518f * y;
        xyb->c[0].p[i] = x * 33.832837186260f;
        xyb->c[1].p[i] = y * 14.458268100570f;
        xyb->c[2].p[i] = b * 49.87984651440f;
    }
}

static bool separate_lf_mf(const Image3F *xyb, Image3F *lf, Image3F *mf,
                           ScratchBuffer *s)
{
    for (int c = 0; c < 3; c++) {
        if (!blur(&xyb->c[c], 7.15593339443f, &lf->c[c], s)) return false;
        sub(&xyb->c[c], &lf->c[c], &mf->c[c]);
    }
    xyb_lf_to_vals(lf);
    return true;
}

static bool suppress_x_by_y(const ImageF *restrict iny, ImageF *restrict x) {
    const size_t n = x->x * x->y;
    for (size_t i = 0; i < n; i++) {
        const float y = iny->p[i];
        const float s = 0.653020556257f;
        x->p[i] *= 46.0f / (y * y + 46.0f) * (1.0f - s) + s;
    }
    return true;
}

static bool separate_mf_hf(Image3F *mf, ImageF hf[2], ScratchBuffer *s) {
    const size_t x = mf->c[0].x, y = mf->c[0].y;
    if (!img_alloc(&hf[0], x, y, s, false) ||
        !img_alloc(&hf[1], x, y, s, false))
        return false;
    for (int c = 0; c < 3; c++) {
        if (c < 2) memcpy(hf[c].p, mf->c[c].p, x * y * sizeof(float));
        if (!blur(&mf->c[c], 3.22489901262f, &mf->c[c], s)) return false;
        if (c == 2) break;
        for (size_t i = 0; i < x * y; i++) {
            const float m = mf->c[c].p[i];
            hf[c].p[i] -= m;
            mf->c[c].p[i] = c == 0 ? remove_range(0.29, m)
                                    : amplify_range(0.1, m);
        }
    }
    return suppress_x_by_y(&hf[1], &hf[0]);
}

static bool separate_hf_uhf(ImageF hf[2], ImageF uhf[2], ScratchBuffer *s) {
    const size_t x = hf[0].x, y = hf[0].y, n = x * y;
    if (!img_alloc(&uhf[0], x, y, s, false) ||
        !img_alloc(&uhf[1], x, y, s, false))
        return false;
    for (int c = 0; c < 2; c++) {
        memcpy(uhf[c].p, hf[c].p, n * sizeof(float));
        if (!blur(&hf[c], 1.56416327805f, &hf[c], s)) return false;
        for (size_t i = 0; i < n; i++) {
            float h = hf[c].p[i];
            float u = uhf[c].p[i] - h;
            if (c == 0) {
                h = remove_range(1.5, h);
                u = remove_range(0.04, u);
            } else {
                h = fmaxclamp(h, 28.4691806922);
                u = fmaxclamp(u, 5.19175294647) * 2.69313763794f;
                h = amplify_range(0.132, h * 2.155f);
            }
            hf[c].p[i] = h;
            uhf[c].p[i] = u;
        }
    }
    return true;
}

static bool separate_freq(const Image3F *xyb, PsychoImage *pi,
                          ScratchBuffer *s)
{
    const size_t x = xyb->c[0].x, y = xyb->c[0].y;
    if (!img3_alloc(&pi->lf, x, y, s, false) ||
        !img3_alloc(&pi->mf, x, y, s, false))
        return false;
    if (!separate_lf_mf(xyb, &pi->lf, &pi->mf, s)) return false;
    if (!separate_mf_hf(&pi->mf, pi->hf, s)) return false;
    return separate_hf_uhf(pi->hf, pi->uhf, s);
}

static float gammaf_ba(const float v) {
    const float vv = fmaxf(0.0f, v);
    return 19.245013259874995f * flogf(vv + 9.9710635769299145f) -
           23.16046239805755f;
}

static void opsin_abs(const float r, const float g, const float b,
                      float *const o0, float *const o1, float *const o2)
{
    *o0 = 0.29956550340058319f * r + 0.63373087833825936f * g
        + 0.077705617820981968f * b + 1.7557483643287353f;
    *o1 = 0.22158691104574774f * r + 0.69391388044116142f * g
        + 0.0987313588422f * b + 1.7557483643287353f;
    *o2 = 0.02f * r + 0.02f * g + 0.20480129041026129f * b
        + 12.226454707163354f;
}

static bool opsin(const Image3F *rgb, const float intensity, Image3F *xyb,
                  ScratchBuffer *s)
{
    const ScratchMark mark = scratch_mark(s);
    Image3F bl = {0};
    const size_t x = rgb->c[0].x, y = rgb->c[0].y, n = x * y;
    if (!img3_alloc(&bl, x, y, s, false)) return false;
    for (int c = 0; c < 3; c++)
        if (!blur(&rgb->c[c], 1.2f, &bl.c[c], s)) {
            scratch_reset(s, mark);
            return false;
        }
    for (size_t i = 0; i < n; i++) {
        float s0, s1, s2, c0, c1, c2;
        opsin_abs(bl.c[0].p[i] * intensity, bl.c[1].p[i] * intensity,
                  bl.c[2].p[i] * intensity, &s0, &s1, &s2);
        if (s0 < 1.7557483643287353f) s0 = 1.7557483643287353f;
        if (s1 < 1.7557483643287353f) s1 = 1.7557483643287353f;
        if (s2 < 12.226454707163354f) s2 = 12.226454707163354f;
        s0 = fmaxf(gammaf_ba(s0) / s0, 1e-4f);
        s1 = fmaxf(gammaf_ba(s1) / s1, 1e-4f);
        s2 = fmaxf(gammaf_ba(s2) / s2, 1e-4f);
        opsin_abs(rgb->c[0].p[i] * intensity, rgb->c[1].p[i] * intensity,
                  rgb->c[2].p[i] * intensity, &c0, &c1, &c2);
        c0 = fmaxf(c0 * s0, 1.7557483643287353f);
        c1 = fmaxf(c1 * s1, 1.7557483643287353f);
        c2 = fmaxf(c2 * s2, 12.226454707163354f);
        xyb->c[0].p[i] = c0 - c1;
        xyb->c[1].p[i] = c0 + c1;
        xyb->c[2].p[i] = c2;
    }
    scratch_reset(s, mark);
    return true;
}

static float pixz(const ImageF *im, const int x, const int y) {
    if (x < 0 || y < 0 || x >= (int)im->x || y >= (int)im->y) return 0.0f;
    return im->p[(size_t)y * im->x + (size_t)x];
}

static float malta_unit(const ImageF *d, int x, int y, const bool lf) {
    static const int off_lf[][18] = {
        {-4,0,-2,0,0,0,2,0,4,0},
        {0,-4,0,-2,0,0,0,2,0,4},
        {-3,-3,-2,-2,0,0,2,2,3,3},
        {3,-3,2,-2,0,0,-2,2,-3,3},
        {1,-4,1,-2,0,0,-1,2,-1,4},
        {-1,-4,-1,-2,0,0,1,2,1,4},
        {-4,-1,-2,-1,0,0,2,1,4,1},
        {-4,1,-2,1,0,0,2,-1,4,-1},
        {-2,-3,-1,-2,0,0,1,2,2,3},
        {2,-3,1,-2,0,0,-1,2,-2,3},
        {-3,-2,-2,-1,0,0,2,1,3,2},
        {3,-2,2,-1,0,0,-2,1,-3,2},
        {-4,2,-2,1,0,0,2,-1,4,-2},
        {-4,-2,-2,-1,0,0,2,1,4,2},
        {-2,-4,-1,-2,0,0,1,2,2,4},
        {2,-4,1,-2,0,0,-1,2,-2,4},
    };
    static const int off[][18] = {
        {-4,0,-3,0,-2,0,-1,0,0,0,1,0,2,0,3,0,4,0},
        {0,-4,0,-3,0,-2,0,-1,0,0,0,1,0,2,0,3,0,4},
        {-3,-3,-2,-2,-1,-1,0,0,1,1,2,2,3,3},
        {3,-3,2,-2,1,-1,0,0,-1,1,-2,2,-3,3},
        {1,-4,1,-3,1,-2,0,-1,0,0,0,1,-1,2,-1,3,-1,4},
        {-1,-4,-1,-3,-1,-2,0,-1,0,0,0,1,1,2,1,3,1,4},
        {-4,-1,-3,-1,-2,-1,-1,0,0,0,1,0,2,1,3,1,4,1},
        {-4,1,-3,1,-2,1,-1,0,0,0,1,0,2,-1,3,-1,4,-1},
        {-2,-3,-1,-2,-1,-1,0,0,1,1,1,2,2,3},
        {2,-3,1,-2,1,-1,0,0,-1,1,-1,2,-2,3},
        {-3,-2,-2,-1,-1,-1,0,0,1,1,2,1,3,2},
        {3,-2,2,-1,1,-1,0,0,-1,1,-2,1,-3,2},
        {-4,1,-3,1,-2,1,-1,0,0,0,1,0,2,-1,3,-1,4,-1},
        {-4,-1,-3,-1,-2,-1,-1,0,0,0,1,0,2,1,3,1,4,1},
        {-1,-4,-1,-3,-1,-2,0,-1,0,0,0,1,1,2,1,3,1,4},
        {1,-4,1,-3,1,-2,0,-1,0,0,0,1,-1,2,-1,3,-1,4},
    };
    float ret = 0.0f;
    for (int i = 0; i < 16; i++) {
        float s = 0.0f;
        const int *o = lf ? off_lf[i] : off[i];
        const int cnt = lf ? 5 : (i < 8 ? 9 : 7);
        for (int j = 0; j < cnt; j++)
            s += pixz(d, x + o[2 * j], y + o[2 * j + 1]);
        ret += s * s;
    }
    return ret;
}

static inline float malta_sum5(const float a, const float b, const float c,
                               const float d, const float e)
{
    const float s = a + b + c + d + e;
    return s * s;
}

static inline float malta_sum7(const float a, const float b, const float c,
                               const float d, const float e, const float f,
                               const float g)
{
    const float s = a + b + c + d + e + f + g;
    return s * s;
}

static inline float malta_sum9(const float a, const float b, const float c,
                               const float d, const float e, const float f,
                               const float g, const float h, const float i)
{
    const float s = a + b + c + d + e + f + g + h + i;
    return s * s;
}

static float malta_unit_inner(const ImageF *d, const int x, const int y,
                              const bool lf)
{
    float ret = 0.0f;
    const size_t stride = d->x;
    const float *p = d->p + (size_t)y * stride + (size_t)x;
    const float *rm4 = p - 4 * stride;
    const float *rm3 = p - 3 * stride;
    const float *rm2 = p - 2 * stride;
    const float *rm1 = p - stride;
    const float *rp1 = p + stride;
    const float *rp2 = p + 2 * stride;
    const float *rp3 = p + 3 * stride;
    const float *rp4 = p + 4 * stride;
    if (lf) {
        ret += malta_sum5(p[-4], p[-2], p[0], p[2], p[4]);
        ret += malta_sum5(rm4[0], rm2[0], p[0], rp2[0], rp4[0]);
        ret += malta_sum5(rm3[-3], rm2[-2], p[0], rp2[2], rp3[3]);
        ret += malta_sum5(rm3[3], rm2[2], p[0], rp2[-2], rp3[-3]);
        ret += malta_sum5(rm4[1], rm2[1], p[0], rp2[-1], rp4[-1]);
        ret += malta_sum5(rm4[-1], rm2[-1], p[0], rp2[1], rp4[1]);
        ret += malta_sum5(rm1[-4], rm1[-2], p[0], rp1[2], rp1[4]);
        ret += malta_sum5(rp1[-4], rp1[-2], p[0], rm1[2], rm1[4]);
        ret += malta_sum5(rm3[-2], rm2[-1], p[0], rp2[1], rp3[2]);
        ret += malta_sum5(rm3[2], rm2[1], p[0], rp2[-1], rp3[-2]);
        ret += malta_sum5(rm2[-3], rm1[-2], p[0], rp1[2], rp2[3]);
        ret += malta_sum5(rm2[3], rm1[2], p[0], rp1[-2], rp2[-3]);
        ret += malta_sum5(rp2[-4], rp1[-2], p[0], rm1[2], rm2[4]);
        ret += malta_sum5(rm2[-4], rm1[-2], p[0], rp1[2], rp2[4]);
        ret += malta_sum5(rm4[-2], rm2[-1], p[0], rp2[1], rp4[2]);
        ret += malta_sum5(rm4[2], rm2[1], p[0], rp2[-1], rp4[-2]);
    } else {
        ret += malta_sum9(p[-4], p[-3], p[-2], p[-1], p[0], p[1],
                          p[2], p[3], p[4]);
        ret += malta_sum9(rm4[0], rm3[0], rm2[0], rm1[0], p[0], rp1[0],
                          rp2[0], rp3[0], rp4[0]);
        ret += malta_sum9(rm3[-3], rm2[-2], rm1[-1], p[0], rp1[1],
                          rp2[2], rp3[3], p[0], p[0]);
        ret += malta_sum9(rm3[3], rm2[2], rm1[1], p[0], rp1[-1],
                          rp2[-2], rp3[-3], p[0], p[0]);
        ret += malta_sum9(rm4[1], rm3[1], rm2[1], rm1[0], p[0], rp1[0],
                          rp2[-1], rp3[-1], rp4[-1]);
        ret += malta_sum9(rm4[-1], rm3[-1], rm2[-1], rm1[0], p[0],
                          rp1[0], rp2[1], rp3[1], rp4[1]);
        ret += malta_sum9(rm1[-4], rm1[-3], rm1[-2], p[-1], p[0], p[1],
                          rp1[2], rp1[3], rp1[4]);
        ret += malta_sum9(rp1[-4], rp1[-3], rp1[-2], p[-1], p[0], p[1],
                          rm1[2], rm1[3], rm1[4]);
        ret += malta_sum7(rm3[-2], rm2[-1], rm1[-1], p[0], rp1[1],
                          rp2[1], rp3[2]);
        ret += malta_sum7(rm3[2], rm2[1], rm1[1], p[0], rp1[-1],
                          rp2[-1], rp3[-2]);
        ret += malta_sum7(rm2[-3], rm1[-2], rm1[-1], p[0], rp1[1],
                          rp1[2], rp2[3]);
        ret += malta_sum7(rm2[3], rm1[2], rm1[1], p[0], rp1[-1],
                          rp1[-2], rp2[-3]);
        ret += malta_sum7(rp1[-4], rp1[-3], rp1[-2], p[-1], p[0], p[1],
                          rm1[2]);
        ret += malta_sum7(rm1[-4], rm1[-3], rm1[-2], p[-1], p[0], p[1],
                          rp1[2]);
        ret += malta_sum7(rm4[-1], rm3[-1], rm2[-1], rm1[0], p[0],
                          rp1[0], rp2[1]);
        ret += malta_sum7(rm4[1], rm3[1], rm2[1], rm1[0], p[0], rp1[0],
                          rp2[-1]);
    }
    return ret;
}

static void malta_diff(const ImageF *restrict a, const ImageF *restrict b,
                       const double w0,
                       const double w1, const double norm1,
                       const bool lf, ImageF *restrict diffs,
                       ImageF *restrict acc)
{
    const double mulli = lf ? 0.611612573796 : 0.39905817637;
    const double len = 3.75;
    const float n0 = (float)(mulli * sqrt(0.5 * w0) / (len * 2 + 1) * norm1);
    const float n1 = (float)(mulli * sqrt(0.33 * w1) / (len * 2 + 1) * norm1);
    for (size_t y = 0; y < a->y; y++) {
        for (size_t x = 0; x < a->x; x++) {
            const size_t i = y * a->x + x;
            const float av = a->p[i], bv = b->p[i];
            const float absval = 0.5f * (fabsf(av) + fabsf(bv));
            float v = n0 * (av - bv) / ((float)norm1 + absval);
            const float sc = n1 / ((float)norm1 + absval);
            const float f0 = fabsf(av), small = 0.55f * f0, big = 1.05f * f0;
            if (av < 0.0f) {
                if (bv > -small) v -= (float)(sc * (bv + small));
                else if (bv < -big) v += (float)(sc * (-bv - big));
            } else {
                if (bv < small) v += (float)(sc * (small - bv));
                else if (bv > big) v -= (float)(sc * (bv - big));
            }
            diffs->p[i] = v;
        }
    }
    for (size_t y = 0; y < a->y; y++)
        for (size_t x = 0; x < a->x; x++)
            acc->p[y * a->x + x] +=
                x >= 4 && y >= 4 && x + 4 < a->x && y + 4 < a->y ?
                malta_unit_inner(diffs, (int)x, (int)y, lf) :
                malta_unit(diffs, (int)x, (int)y, lf);
}

static void l2diff(const ImageF *restrict a, const ImageF *restrict b,
                   const float w, ImageF *restrict out, const bool set)
{
    const size_t n = a->x * a->y;
    if (w == 0.0f) return;
    for (size_t i = 0; i < n; i++) {
        const float d = a->p[i] - b->p[i];
        if (set) out->p[i] = d * d * w;
        else out->p[i] += d * d * w;
    }
}

static void l2diff_asym(const ImageF *restrict a, const ImageF *restrict b,
                        const float w0, const float w1,
                        ImageF *restrict out)
{
    const size_t n = a->x * a->y;
    for (size_t i = 0; i < n; i++) {
        const float av = a->p[i], bv = b->p[i], d = av - bv;
        float total = out->p[i] + d * d * w0 * 0.8f;
        const float f0 = fabsf(av), small = 0.4f * f0, big = f0;
        float v;
        if (av < 0.0f) {
            if (bv > -small) v = bv + small;
            else if (bv < -big) v = -bv - big;
            else v = 0.0f;
        } else {
            if (bv < small) v = small - bv;
            else if (bv > big) v = bv - big;
            else v = 0.0f;
        }
        out->p[i] = total + w1 * 0.8f * v * v;
    }
}

static void combine_mask(const ImageF *restrict hf,
                         const ImageF *restrict uhf, ImageF *restrict out)
{
    const size_t n = out->x * out->y;
    for (size_t i = 0; i < n; i++) {
        const float xd = (uhf[0].p[i] + hf[0].p[i]) * 2.5f;
        const float yd = uhf[1].p[i] * 0.4f + hf[1].p[i] * 0.4f;
        out->p[i] = sqrtf(xd * xd + yd * yd);
    }
}

static void store_min3(float v, float *m0, float *m1, float *m2) {
    if (v >= *m2) return;
    if (v < *m0) {
        *m2 = *m1;
        *m1 = *m0;
        *m0 = v;
    } else if (v < *m1) {
        *m2 = *m1;
        *m1 = v;
    } else {
        *m2 = v;
    }
}

static Float4 load4(const float *p) {
    Float4 v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static void store4(float *const p, const Float4 v) {
    memcpy(p, &v, sizeof(v));
}

static Float4 select4(const Int4 m, const Float4 a, const Float4 b) {
    const Vec4 mask = {.i = m}, av = {.f = a}, bv = {.f = b};
    const Vec4 v = {.i = (mask.i & av.i) | (~mask.i & bv.i)};
    return v.f;
}

static void store_min3x4(const Float4 v, Float4 *const m0, Float4 *const m1,
                         Float4 *const m2)
{
    const Float4 a = *m0, b = *m1, c = *m2;
    const Int4 take = ~(v >= c), lt0 = take & (v < a);
    const Int4 lt1 = take & ~lt0 & (v < b);
    const Int4 last = take & ~lt0 & ~lt1;
    *m0 = select4(lt0, v, a);
    *m1 = select4(lt0, a, select4(lt1, v, b));
    *m2 = select4(lt0, b, select4(lt1, b, select4(last, v, c)));
}

static float fuzzy_erosion_pixel(const ImageF *from, const size_t x,
                                 const size_t y)
{
    const int st = 3;
    float m0 = pixz(from, (int)x, (int)y), m1 = 2 * m0, m2 = m1;
    if (x >= (size_t)st) {
        store_min3(pixz(from, (int)x - st, (int)y), &m0, &m1, &m2);
        if (y >= (size_t)st)
            store_min3(pixz(from, (int)x - st, (int)y - st),
                       &m0, &m1, &m2);
        if (y < from->y - (size_t)st)
            store_min3(pixz(from, (int)x - st, (int)y + st),
                       &m0, &m1, &m2);
    }
    if (x < from->x - (size_t)st) {
        store_min3(pixz(from, (int)x + st, (int)y), &m0, &m1, &m2);
        if (y >= (size_t)st)
            store_min3(pixz(from, (int)x + st, (int)y - st),
                       &m0, &m1, &m2);
        if (y < from->y - (size_t)st)
            store_min3(pixz(from, (int)x + st, (int)y + st),
                       &m0, &m1, &m2);
    }
    if (y >= (size_t)st)
        store_min3(pixz(from, (int)x, (int)y - st), &m0, &m1, &m2);
    if (y < from->y - (size_t)st)
        store_min3(pixz(from, (int)x, (int)y + st), &m0, &m1, &m2);
    return 0.45f * m0 + 0.3f * m1 + 0.25f * m2;
}

static void fuzzy_erosion(const ImageF *restrict from, ImageF *restrict to) {
    const size_t st = 3, width = from->x, height = from->y;
    for (size_t y = 0; y < height; y++) {
        size_t x = 0;
        if (y >= st && y + st < height && width > 2 * st) {
            for (; x < st; x++)
                to->p[y * width + x] = fuzzy_erosion_pixel(from, x, y);
            for (; x + 4 <= width - st; x += 4) {
                Float4 m0 = load4(from->p + y * width + x);
                Float4 m1 = 2.0f * m0, m2 = m1;
                store_min3x4(load4(from->p + y * width + x - st),
                             &m0, &m1, &m2);
                store_min3x4(load4(from->p + (y - st) * width + x - st),
                             &m0, &m1, &m2);
                store_min3x4(load4(from->p + (y + st) * width + x - st),
                             &m0, &m1, &m2);
                store_min3x4(load4(from->p + y * width + x + st),
                             &m0, &m1, &m2);
                store_min3x4(load4(from->p + (y - st) * width + x + st),
                             &m0, &m1, &m2);
                store_min3x4(load4(from->p + (y + st) * width + x + st),
                             &m0, &m1, &m2);
                store_min3x4(load4(from->p + (y - st) * width + x),
                             &m0, &m1, &m2);
                store_min3x4(load4(from->p + (y + st) * width + x),
                             &m0, &m1, &m2);
                store4(to->p + y * width + x,
                       0.45f * m0 + 0.3f * m1 + 0.25f * m2);
            }
        }
        for (; x < width; x++)
            to->p[y * width + x] = fuzzy_erosion_pixel(from, x, y);
    }
}

static bool mask_img(ImageF *m0, ImageF *m1, ImageF *mask,
                     ImageF *diff_ac, ScratchBuffer *s)
{
    const ScratchMark mark = scratch_mark(s);
    const size_t x = m0->x, y = m0->y;
    for (size_t i = 0; i < x * y; i++) {
        m0->p[i] = diff_pre_val(m0->p[i]);
        m1->p[i] = diff_pre_val(m1->p[i]);
    }
    if (!blur(m0, 2.7f, m0, s) || !blur(m1, 2.7f, m1, s)) {
        scratch_reset(s, mark);
        return false;
    }
    for (size_t i = 0; i < x * y; i++) {
        if (diff_ac != NULL) {
            const float d = m0->p[i] - m1->p[i];
            diff_ac->p[i] += 10.0f * d * d;
        }
    }
    fuzzy_erosion(m0, mask);
    scratch_reset(s, mark);
    return true;
}

static double mask_y(const double delta) {
    const double c = 2.5485944793 / (0.451936922203 * delta
                   + 0.829591754942);
    const double v = GLOBAL_SCALE * (1.0 + c);
    return v * v;
}

static double mask_dc_y(const double delta) {
    const double c = 0.505054525019 / (3.87449418804 * delta
                   + 0.20025578522);
    const double v = GLOBAL_SCALE * (1.0 + c);
    return v * v;
}

static bool diffmap_psycho(const PsychoImage *p0, const PsychoImage *p1,
                           const float hfasy, ImageF *dm, ScratchBuffer *s)
{
    const ScratchMark mark = scratch_mark(s);
    const size_t x = dm->x, y = dm->y;
    ImageF diffs = {0}, mask = {0}, m0 = {0}, m1 = {0};
    Image3F ac = {0}, dc = {0};
    if (!img_alloc(&diffs, x, y, s, false) ||
        !img3_alloc(&ac, x, y, s, true) ||
        !img3_alloc(&dc, x, y, s, true) ||
        !img_alloc(&mask, x, y, s, false) ||
        !img_alloc(&m0, x, y, s, false) ||
        !img_alloc(&m1, x, y, s, false))
    {
        scratch_reset(s, mark);
        return false;
    }
    const float sh = sqrtf(hfasy);
    malta_diff(&p0->uhf[1], &p1->uhf[1], W_UHF_MALTA * hfasy,
               W_UHF_MALTA / hfasy, NORM_1_UHF, false, &diffs, &ac.c[1]);
    malta_diff(&p0->uhf[0], &p1->uhf[0], W_UHF_MALTA_X * hfasy,
               W_UHF_MALTA_X / hfasy, NORM_1_UHF_X, false, &diffs, &ac.c[0]);
    malta_diff(&p0->hf[1], &p1->hf[1], W_HF_MALTA * sh, W_HF_MALTA / sh,
               NORM_1_HF, true, &diffs, &ac.c[1]);
    malta_diff(&p0->hf[0], &p1->hf[0], W_HF_MALTA_X * sh, W_HF_MALTA_X / sh,
               NORM_1_HF_X, true, &diffs, &ac.c[0]);
    malta_diff(&p0->mf.c[1], &p1->mf.c[1], W_MF_MALTA, W_MF_MALTA,
               NORM_1_MF, true, &diffs, &ac.c[1]);
    malta_diff(&p0->mf.c[0], &p1->mf.c[0], W_MF_MALTA_X, W_MF_MALTA_X,
               NORM1_MF_X, true, &diffs, &ac.c[0]);
    for (int c = 0; c < 3; c++) {
        if (c < 2)
            l2diff_asym(&p0->hf[c], &p1->hf[c], WMUL[c] * hfasy,
                        WMUL[c] / hfasy, &ac.c[c]);
        l2diff(&p0->mf.c[c], &p1->mf.c[c], (float)WMUL[3 + c],
               &ac.c[c], false);
        l2diff(&p0->lf.c[c], &p1->lf.c[c], (float)WMUL[6 + c],
               &dc.c[c], true);
    }
    combine_mask(p0->hf, p0->uhf, &m0);
    combine_mask(p1->hf, p1->uhf, &m1);
    if (!mask_img(&m0, &m1, &mask, &ac.c[1], s)) {
        scratch_reset(s, mark);
        return false;
    }
    for (size_t i = 0; i < x * y; i++) {
        const double m = mask.p[i];
        const double am = mask_y(m), dmw = mask_dc_y(m);
        double dcv = 0.0, acv = 0.0;
        for (int c = 0; c < 3; c++) {
            dcv += dc.c[c].p[i] * dmw;
            acv += ac.c[c].p[i] * am;
        }
        dm->p[i] = (float)sqrt(fmax(0.0, dcv + acv));
    }
    scratch_reset(s, mark);
    return true;
}

static bool subsample2(const Image3F *in, Image3F *out, ScratchBuffer *s) {
    const size_t xs = (in->c[0].x + 1) / 2, ys = (in->c[0].y + 1) / 2;
    if (!img3_alloc(out, xs, ys, s, true)) return false;
    for (int c = 0; c < 3; c++) {
        for (size_t y = 0; y < in->c[c].y; y++)
            for (size_t x = 0; x < in->c[c].x; x++)
                out->c[c].p[(y / 2) * xs + x / 2] +=
                    0.25f * in->c[c].p[y * in->c[c].x + x];
        if ((in->c[c].x & 1) != 0)
            for (size_t y = 0; y < ys; y++) out->c[c].p[y * xs + xs - 1] *= 2;
        if ((in->c[c].y & 1) != 0)
            for (size_t x = 0; x < xs; x++) out->c[c].p[(ys - 1) * xs + x] *= 2;
    }
    return true;
}

static void add_sup2(const ImageF *restrict src, const float w,
                     ImageF *restrict dst)
{
    for (size_t y = 0; y < dst->y; y++) {
        for (size_t x = 0; x < dst->x; x++) {
            float *v = &dst->p[y * dst->x + x];
            *v *= 1.0f - 0.3f * w;
            *v += w * src->p[(y / 2) * src->x + x / 2];
        }
    }
}

static bool butter_diffmap(const Image3F *r0, const Image3F *r1,
                           const float intensity, ImageF *dm, ScratchBuffer *s)
{
    const ScratchMark mark = scratch_mark(s);
    const size_t x = r0->c[0].x, y = r0->c[0].y;
    if (x < 8 || y < 8) {
        memset(dm->p, 0, x * y * sizeof(float));
        return true;
    }
    Image3F x0 = {0}, x1 = {0};
    PsychoImage p0 = {0}, p1 = {0};
    if (!img3_alloc(&x0, x, y, s, false) ||
        !img3_alloc(&x1, x, y, s, false))
        return false;
    if (!opsin(r0, intensity, &x0, s) || !opsin(r1, intensity, &x1, s))
        return false;
    if (!separate_freq(&x0, &p0, s) || !separate_freq(&x1, &p1, s))
        return false;
    if (!diffmap_psycho(&p0, &p1, 1.0f, dm, s)) return false;
    Image3F s0 = {0}, s1 = {0};
    ImageF sub = {0};
    if (subsample2(r0, &s0, s) && subsample2(r1, &s1, s) &&
        img_alloc(&sub, s0.c[0].x, s0.c[0].y, s, false))
    {
        if (s0.c[0].x >= 8 && s0.c[0].y >= 8 &&
            butter_diffmap(&s0, &s1, intensity, &sub, s))
        {
            add_sup2(&sub, 0.5f, dm);
        }
    }
    scratch_reset(s, mark);
    return true;
}

static double pnorm_score(const ImageF *dm, const int pnorm) {
    const double p = pnorm <= 0 ? 3.0 : (double)pnorm;
    double sum[3] = {0.0, 0.0, 0.0};
    const size_t n = dm->x * dm->y;
    if (fabs(p - 3.0) < 1e-6)
        for (size_t i = 0; i < n; i++) {
            double d = dm->p[i], d2 = d * d * d;
            sum[0] += d2;
            d2 *= d2;
            sum[1] += d2;
            d2 *= d2;
            sum[2] += d2;
        }
    else
        for (size_t i = 0; i < n; i++) {
            double d2 = pow(dm->p[i], p);
            sum[0] += d2;
            d2 *= d2;
            sum[1] += d2;
            d2 *= d2;
            sum[2] += d2;
        }
    double v = 0.0;
    for (int i = 0; i < 3; i++)
        v += pow(sum[i] / (double)n, 1.0 / (p * (double)(1 << i)));
    return v / 3.0;
}

static FmetricsErr validate(const FmetricsImg *r, const FmetricsImg *d,
                            const FmetricsButteraugliOptions *o,
                            const double *result)
{
    if (r == NULL || d == NULL || o == NULL || result == NULL ||
        r->data == NULL || d->data == NULL)
    {
        return FMETRICS_ERR_INVALID_ARGUMENT;
    }
    const FmetricsImg *images[2] = {r, d};
    for (unsigned i = 0; i < 2; i++) {
        const FmetricsImg *img = images[i];
        const FmetricsErr error = fmetrics_validate_rgb(img);
        if (error != FMETRICS_OK) return error;
    }
    if (r->width != d->width || r->height != d->height)
        return FMETRICS_ERR_DIMENSION_MISMATCH;
    if (r->width == 0 || r->height == 0 || r->stride < r->width * 3 ||
        d->stride < d->width * 3 || o->intensity_target <= 0.0f)
    {
        return FMETRICS_ERR_INVALID_ARGUMENT;
    }
    return FMETRICS_OK;
}

static size_t butteraugli_scratch_size(const size_t x, const size_t y) {
    return x * y * sizeof(float) * 128;
}

static bool load_rgb(const FmetricsImg *src, Image3F *dst, ScratchBuffer *s) {
    if (!img3_alloc(dst, src->width, src->height, s, false)) return false;
    for (uint32_t y = 0; y < src->height; y++) {
        const uint8_t *const r =
            (const uint8_t *)src->data + (size_t)y * src->stride;
        for (uint32_t x = 0; x < src->width; x++) {
            const size_t i = (size_t)y * src->width + x;
            for (unsigned ch = 0; ch < 3; ch++) {
                float v;
                if (src->format == FMETRICS_PIX_FMT_RGB_FLOAT)
                    memcpy(&v, r + (x * 3 + ch) * sizeof(float), sizeof(v));
                else v = SRGB_LUT[r[x * 3 + ch]];
                dst->c[ch].p[i] = v;
            }
        }
    }
    return true;
}

static void make_map(const ImageF *dm, uint32_t *map) {
    const size_t n = dm->x * dm->y;
    for (size_t i = 0; i < n; i++) {
        const int v =
            iclip((int)lrint(fminf(1.0f, dm->p[i] / 4.0f) * 255.0f), 0, 255);
        map[i] = TURBO_COLORMAP[v];
    }
}

static FmetricsErr butteraugli_cmp(const FmetricsImg *const reference,
                                   const FmetricsImg *const distorted,
                                   const FmetricsButteraugliOptions *const o,
                                   double *const result,
                                   uint32_t *const error_map,
                                   const bool output_map,
                                   FmetricsWorkspace *const workspace)
{
    const FmetricsErr err = validate(reference, distorted, o, result);
    if (err != FMETRICS_OK) return err;
    if (workspace == NULL || (output_map && error_map == NULL))
        return FMETRICS_ERR_INVALID_ARGUMENT;
    if (!workspace_reserve(workspace,
                           butteraugli_scratch_size(reference->width,
                                                   reference->height)))
    {
        return FMETRICS_ERR_OUT_OF_MEMORY;
    }
    ScratchBuffer *const scratch = &workspace->scratch;
    Image3F r = {0}, d = {0};
    ImageF dm = {0};
    if (!load_rgb(reference, &r, scratch) ||
        !load_rgb(distorted, &d, scratch) ||
        !img_alloc(&dm, reference->width, reference->height, scratch, false))
    {
        return FMETRICS_ERR_OUT_OF_MEMORY;
    }
    if (!butter_diffmap(&r, &d, o->intensity_target, &dm, scratch))
        return FMETRICS_ERR_OUT_OF_MEMORY;
    *result = pnorm_score(&dm, o->pnorm);
    if (error_map != NULL) make_map(&dm, error_map);
    return FMETRICS_OK;
}

FmetricsErr fmetrics_butteraugli_cmp(FmetricsWorkspace *const workspace,
                                     const FmetricsImg *const reference,
                                     const FmetricsImg *const distorted,
                                     const FmetricsButteraugliOptions *const o,
                                     double *const result)
{
    return butteraugli_cmp(reference, distorted, o, result, NULL, false,
                           workspace);
}

FmetricsErr fmetrics_butteraugli_cmp_map(
    FmetricsWorkspace *const workspace, const FmetricsImg *const reference,
    const FmetricsImg *const distorted,
    const FmetricsButteraugliOptions *const o, double *const result,
    uint32_t *const error_map)
{
    return butteraugli_cmp(reference, distorted, o, result, error_map, true,
                          workspace);
}

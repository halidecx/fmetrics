#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

#include "../common/mem.h"
#include "../common/util.h"
#include "../fmetrics.h"

static FmetricsErr validate_pair(const FmetricsImg *const reference,
                                 const FmetricsImg *const distorted,
                                 const double *const result)
{
    if (result == NULL || reference == NULL || distorted == NULL ||
        reference->data == NULL || distorted->data == NULL)
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
    if (reference->width < 8 || reference->height < 8)
        return FMETRICS_ERR_IWSSIM_IMG_TOO_SMALL;
    if (reference->stride < reference->width * 3 ||
        distorted->stride < distorted->width * 3)
    {
        return FMETRICS_ERR_INVALID_ARGUMENT;
    }
    return FMETRICS_OK;
}

static bool load_luma(const FmetricsImg *const src, ImageD *const dst,
                      ScratchBuffer *const scratch)
{
    if (!image_alloc(dst, (int)src->width, (int)src->height, scratch))
        return false;
    const float kr = 0.299f / 255.0f;
    const float kg = 0.587f / 255.0f;
    const float kb = 0.114f / 255.0f;
    for (size_t y = 0; y < src->height; y++) {
        const uint8_t *row = src->data + y * (size_t)src->stride;
        float *out = dst->data + y * (size_t)dst->width;
        for (size_t x = 0; x < src->width; x++) {
            const uint8_t *px = row + x * 3u;
            out[x] = kr * px[0] + kg * px[1] + kb * px[2];
        }
    }
    return true;
}

static void gaussian_kernel(float k[9]) {
    const float scale = -1.0f / (2.0f * 1.5f * 1.5f);
    float sum = 0.0f;
    for (int i = -4; i <= 4; i++) {
        const float v = expf(scale * (float)(i * i));
        k[i + 4] = v;
        sum += v;
    }
    for (int i = 0; i < 9; i++) k[i] /= sum;
}

static inline float sample_h(const float *const row, const int x,
                             const int w)
{
    return row[iclip(x, 0, w - 1)];
}

static void convolve_h(const ImageD *const in, ImageD *const out,
                       const float k[9])
{
    const int w = in->width;
    const int h = in->height;
    float kloc[5];
    for (int i = 0; i <= 4; i++) kloc[i] = k[4 + i];
    for (int y = 0; y < h; y++) {
        const float *restrict row = in->data + (size_t)y * w;
        float *restrict dst = out->data + (size_t)y * w;
        int x = 0;
        for (; x < imin(4, w); x++) {
            float sum = kloc[0] * row[x];
            for (int i = 1; i <= 4; i++)
                sum += kloc[i] * (sample_h(row, x - i, w) +
                                  sample_h(row, x + i, w));
            dst[x] = sum;
        }
        for (; x < w - 4; x++) {
            float sum = kloc[0] * row[x];
            for (int i = 1; i <= 4; i++)
                sum += kloc[i] * (row[x - i] + row[x + i]);
            dst[x] = sum;
        }
        for (; x < w; x++) {
            float sum = kloc[0] * row[x];
            for (int i = 1; i <= 4; i++)
                sum += kloc[i] * (sample_h(row, x - i, w) +
                                  sample_h(row, x + i, w));
            dst[x] = sum;
        }
    }
}

static void convolve_v(const ImageD *const in, ImageD *const out,
                       const float k[9])
{
    const int w = in->width;
    const int h = in->height;
    float kloc[5];
    for (int i = 0; i <= 4; i++) kloc[i] = k[4 + i];
    for (int y = 0; y < h; y++) {
        float *restrict dst = out->data + (size_t)y * w;
        const float *restrict row = in->data + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            float sum = kloc[0] * row[x];
            for (int i = 1; i <= 4; i++) {
                const int uy = iclip(y - i, 0, h - 1);
                const int dy = iclip(y + i, 0, h - 1);
                sum += kloc[i] * (in->data[(size_t)uy * w + x] +
                                  in->data[(size_t)dy * w + x]);
            }
            dst[x] = sum;
        }
    }
}

static void blur(const ImageD *const in, ImageD *const out,
                 ImageD *const tmp, const float k[9])
{
    tmp->width = in->width;
    tmp->height = in->height;
    out->width = in->width;
    out->height = in->height;
    convolve_h(in, tmp, k);
    convolve_v(tmp, out, k);
}

static void downsample_avg2(const ImageD src, ImageD *const out)
{
    const int ow = out->width;
    const int oh = out->height;
    for (int y = 0; y < oh; y++) {
        const int y0 = y * 2;
        const int y1 = y0 + 1 < src.height ? y0 + 1 : y0;
        const float *r0 = src.data + (size_t)y0 * src.width;
        const float *r1 = src.data + (size_t)y1 * src.width;
        float *dst = out->data + (size_t)y * ow;
        for (int x = 0; x < ow; x++) {
            const int x0 = x * 2;
            const int x1 = x0 + 1 < src.width ? x0 + 1 : x0;
            dst[x] = (r0[x0] + r0[x1] + r1[x0] + r1[x1]) * 0.25f;
        }
    }
}

static double msssim_score(ImageD *const img1, ImageD *const img2,
                           ScratchBuffer *const scratch)
{
    ImageD mu1 = {0}, mu2 = {0}, tmp = {0};
    ImageD sigma1_sq = {0}, sigma2_sq = {0}, sigma12 = {0};
    if (!image_alloc(&mu1, img1->width, img1->height, scratch) ||
        !image_alloc(&mu2, img1->width, img1->height, scratch) ||
        !image_alloc(&tmp, img1->width, img1->height, scratch) ||
        !image_alloc(&sigma1_sq, img1->width, img1->height, scratch) ||
        !image_alloc(&sigma2_sq, img1->width, img1->height, scratch) ||
        !image_alloc(&sigma12, img1->width, img1->height, scratch))
    {
        return NAN;
    }

    const double weights[5] = { 0.0448, 0.2856, 0.3001, 0.2363, 0.1333 };
    const float c1 = 0.0001f;
    const float c2 = 0.0009f;
    float kernel[9];
    gaussian_kernel(kernel);

    double log_msssim = 0.0;
    double weights_used = 0.0;
    for (int j = 0; j < 5; j++) {
        const int sx = img1->width;
        const int sy = img1->height;
        if (sx < 8 || sy < 8) break;

        blur(img1, &mu1, &tmp, kernel);
        blur(img2, &mu2, &tmp, kernel);

        const size_t n = (size_t)sx * sy;
        for (size_t i = 0; i < n; i++) tmp.data[i] = img1->data[i] *
                                                     img1->data[i];
        blur(&tmp, &sigma1_sq, &mu1, kernel);
        for (size_t i = 0; i < n; i++) tmp.data[i] = img2->data[i] *
                                                     img2->data[i];
        blur(&tmp, &sigma2_sq, &mu1, kernel);
        for (size_t i = 0; i < n; i++) tmp.data[i] = img1->data[i] *
                                                     img2->data[i];
        blur(&tmp, &sigma12, &mu1, kernel);
        blur(img1, &mu1, &tmp, kernel);

        double ssim_sum = 0.0;
        double cs_sum = 0.0;
        const double norm = 1.0 / (double)n;
        for (size_t i = 0; i < n; i++) {
            const float m1 = mu1.data[i];
            const float m2 = mu2.data[i];
            const float m1m2 = m1 * m2;
            const float m1sq = m1 * m1;
            const float m2sq = m2 * m2;
            const float s1sq = sigma1_sq.data[i] - m1sq;
            const float s2sq = sigma2_sq.data[i] - m2sq;
            const float s12 = sigma12.data[i] - m1m2;
            const float l_num = 2.0f * m1m2 + c1;
            const float l_den = m1sq + m2sq + c1;
            const float cs_num = 2.0f * s12 + c2;
            const float cs_den = s1sq + s2sq + c2;
            const float cs = cs_num / cs_den;
            ssim_sum += (l_num / l_den) * cs;
            cs_sum += cs;
        }

        const bool last = j == 4 || sx / 2 < 8 || sy / 2 < 8;
        double contribution = (last ? ssim_sum : cs_sum) * norm;
        if (contribution < 1e-10) contribution = 1e-10;
        log_msssim += weights[j] * log(contribution);
        weights_used += weights[j];
        if (last) break;

        const ImageD src1 = *img1;
        const ImageD src2 = *img2;
        img1->width = (sx + 1) / 2;
        img1->height = (sy + 1) / 2;
        img2->width = img1->width;
        img2->height = img1->height;
        downsample_avg2(src1, img1);
        downsample_avg2(src2, img2);
    }

    if (weights_used <= 0.0) return 0.0;
    return exp(log_msssim / weights_used);
}

static size_t msssim_scratch_size(const uint32_t width, const uint32_t height) {
    return (size_t)width * height * sizeof(float) * 9 + 4096;
}

FmetricsErr fmetrics_msssim_cmp(const FmetricsImg *const reference,
                                const FmetricsImg *const distorted,
                                double *const result)
{
    const FmetricsErr valid = validate_pair(reference, distorted, result);
    if (valid != FMETRICS_OK) return valid;

    const size_t scratch_size = msssim_scratch_size(reference->width,
                                                   reference->height);
    ScratchBuffer scratch;
    scratch.data = malloc(scratch_size);
    if (scratch.data == NULL) return FMETRICS_ERR_OUT_OF_MEMORY;
    scratch.size = scratch_size;
    scratch.offset = 0;

    ImageD img1 = {0}, img2 = {0};
    if (!load_luma(reference, &img1, &scratch) ||
        !load_luma(distorted, &img2, &scratch))
    {
        free(scratch.data);
        return FMETRICS_ERR_OUT_OF_MEMORY;
    }

    const double score = msssim_score(&img1, &img2, &scratch);
    if (isnan(score)) {
        free(scratch.data);
        return FMETRICS_ERR_OUT_OF_MEMORY;
    }
    *result = fclip(score, 0.0, 1.0);
    free(scratch.data);
    return FMETRICS_OK;
}

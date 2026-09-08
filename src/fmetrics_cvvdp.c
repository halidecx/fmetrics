#include "cvvdp.h"
#include "fmetrics.h"
#include <limits.h>
#include <stdlib.h>
#include "common/color.h"

struct FmetricsCvvdpCtx {
    FcvvdpCtx *backend;
    bool hdr;
    float *pixels;
    size_t capacity;
};

static FmetricsErr map_error(const FcvvdpError e) {
    switch (e) {
    case CVVDP_OK:
        return FMETRICS_OK;
    case CVVDP_ERROR_INVALID_FORMAT:
        return FMETRICS_ERR_UNSUPPORTED_FORMAT;
    case CVVDP_ERROR_OUT_OF_MEMORY:
        return FMETRICS_ERR_OUT_OF_MEMORY;
    case CVVDP_ERROR_DIMENSION_MISMATCH:
        return FMETRICS_ERR_DIMENSION_MISMATCH;
    case CVVDP_ERROR_NULL_POINTER:
    case CVVDP_ERROR_INVALID_DIMENSIONS:
    case CVVDP_ERROR_INVALID_MODEL:
    case CVVDP_ERROR_NOT_INITIALIZED:
        return FMETRICS_ERR_INVALID_ARGUMENT;
    default:
        return FMETRICS_ERR_INTERNAL;
    }
}

static FcvvdpPixFmt pixel_format(const FmetricsPixFmt format) {
    switch (format) {
    case FMETRICS_PIX_FMT_RGB_FLOAT:
        return CVVDP_PIXEL_FORMAT_RGB_FLOAT;
    case FMETRICS_PIX_FMT_RGB_UINT8:
        return CVVDP_PIXEL_FORMAT_RGB_UINT8;
    case FMETRICS_PIX_FMT_RGB_UINT16:
        return CVVDP_PIXEL_FORMAT_RGB_UINT16;
    default:
        return (FcvvdpPixFmt)-1;
    }
}

static FcvvdpDisplayParams display_params(
    const FmetricsCvvdpDisplayParams *const p)
{
    return (FcvvdpDisplayParams){
        .resolution_width = p->resolution_width,
        .resolution_height = p->resolution_height,
        .viewing_distance_meters = p->viewing_distance_meters,
        .diagonal_size_inches = p->diagonal_size_inches,
        .max_luminance = p->max_luminance,
        .contrast = p->contrast,
        .ambient_light = p->ambient_light,
        .reflectivity = p->reflectivity,
        .is_hdr = p->is_hdr,
    };
}

static FcvvdpImage image(const FmetricsImg *const img) {
    return (FcvvdpImage){
        .width = (int)img->width,
        .height = (int)img->height,
        .stride = (int)img->stride,
        .data = img->data,
        .format = pixel_format(img->format),
        .colorspace = fmetrics_is_linear(img) ? CVVDP_COLORSPACE_LINEAR :
            CVVDP_COLORSPACE_SRGB,
    };
}

static FmetricsErr validate_image(const FmetricsImg *const img) {
    if (img->data == NULL || img->width == 0 || img->height == 0)
        return FMETRICS_ERR_INVALID_ARGUMENT;
    uint32_t bytes_per_pixel;
    switch (img->format) {
    case FMETRICS_PIX_FMT_RGB_FLOAT:
        bytes_per_pixel = 12;
        break;
    case FMETRICS_PIX_FMT_RGB_UINT8:
        bytes_per_pixel = 3;
        break;
    case FMETRICS_PIX_FMT_RGB_UINT16:
        bytes_per_pixel = 6;
        break;
    default:
        return FMETRICS_ERR_UNSUPPORTED_FORMAT;
    }
    if (img->colorspace != FMETRICS_COLORSPACE_SRGB &&
        !fmetrics_is_linear(img))
        return FMETRICS_ERR_UNSUPPORTED_FORMAT;
    if (img->width > INT_MAX || img->height > INT_MAX ||
        img->stride > INT_MAX)
    {
        return FMETRICS_ERR_INVALID_ARGUMENT;
    }
    if (img->stride != 0 &&
        img->stride < (uint64_t)img->width * bytes_per_pixel)
    {
        return FMETRICS_ERR_INVALID_ARGUMENT;
    }
    if (fmetrics_is_linear(img)) {
        FmetricsImg packed = *img;
        if (packed.stride == 0) packed.stride = packed.width * 12;
        return fmetrics_validate_rgb(&packed);
    }
    return FMETRICS_OK;
}

static FmetricsErr images(const FmetricsImg *const reference,
                          const FmetricsImg *const distorted,
                          FcvvdpImage *const ref_img,
                          FcvvdpImage *const dis_img,
                          const FcvvdpImage **const ref_ptr,
                          const FcvvdpImage **const dis_ptr,
                          const bool hdr, float **pixels, size_t *capacity)
{
    *ref_ptr = NULL;
    *dis_ptr = NULL;
    if (reference != NULL) {
        const FmetricsErr error = validate_image(reference);
        if (error != FMETRICS_OK) return error;
        *ref_img = image(reference);
        *ref_ptr = ref_img;
    }
    if (distorted != NULL) {
        const FmetricsErr error = validate_image(distorted);
        if (error != FMETRICS_OK) return error;
        *dis_img = image(distorted);
        *dis_ptr = dis_img;
    }
    if (hdr && reference && distorted &&
        (fmetrics_is_linear(reference) || fmetrics_is_linear(distorted)))
    {
        const size_t ref_count = (size_t)reference->width * reference->height;
        const size_t dis_count = (size_t)distorted->width * distorted->height;
        if (dis_count > SIZE_MAX / 12 ||
            ref_count > SIZE_MAX / 12 - dis_count)
            return FMETRICS_ERR_OUT_OF_MEMORY;
        const size_t bytes = (ref_count + dis_count) * 12;
        if (*capacity < bytes) {
            float *buffer = realloc(*pixels, bytes);
            if (!buffer) return FMETRICS_ERR_OUT_OF_MEMORY;
            *pixels = buffer;
            *capacity = bytes;
        }
        const FmetricsImg *src[2] = {reference, distorted};
        FcvvdpImage *dst[2] = {ref_img, dis_img};
        float *out = *pixels;
        for (unsigned i = 0; i < 2; i++) {
            const unsigned bpp = src[i]->format ==
                FMETRICS_PIX_FMT_RGB_UINT8 ? 3 :
                src[i]->format == FMETRICS_PIX_FMT_RGB_UINT16 ? 6 : 12;
            const size_t stride = src[i]->stride ? src[i]->stride :
                (size_t)src[i]->width * bpp;
            for (uint32_t y = 0; y < src[i]->height; y++) {
                const uint8_t *row = (const uint8_t *)src[i]->data +
                    y * stride;
                for (size_t x = 0; x < (size_t)src[i]->width * 3; x++) {
                    float value;
                    if (bpp == 12) {
                        memcpy(&value, row + x * 4, 4);
                        if (src[i]->colorspace == FMETRICS_COLORSPACE_SRGB)
                            value = fmetrics_srgb_linear(value);
                    } else if (bpp == 6) {
                        uint16_t v;
                        memcpy(&v, row + x * 2, 2);
                        value = fmetrics_srgb_linear(v / 65535.0f);
                    } else value = fmetrics_srgb_linear(row[x] / 255.0f);
                    out[(size_t)y * src[i]->width * 3 + x] = value * 2.03f;
                }
            }
            dst[i]->data = out;
            dst[i]->stride = src[i]->width * 12;
            dst[i]->format = CVVDP_PIXEL_FORMAT_RGB_FLOAT;
            dst[i]->colorspace = CVVDP_COLORSPACE_LINEAR;
            out += (size_t)src[i]->width * src[i]->height * 3;
        }
    }
    return FMETRICS_OK;
}

FmetricsErr fmetrics_cvvdp_create(
    const int width, const int height, const float fps,
    const FmetricsCvvdpDisplayModel display_model, const unsigned threads,
    const FmetricsCvvdpDisplayParams *const custom_params,
    FmetricsCvvdpCtx **const out_context)
{
    FcvvdpDisplayParams params;
    const FcvvdpDisplayParams *params_ptr = NULL;
    if (custom_params != NULL &&
        display_model == FMETRICS_CVVDP_DISPLAY_CUSTOM)
    {
        params = display_params(custom_params);
        params_ptr = &params;
    }
    if (out_context == NULL)
        return map_error(cvvdp_create(
            width, height, fps, (FcvvdpDisplayModel)display_model, threads,
            params_ptr, NULL));
    FcvvdpCtx *context = NULL;
    const FcvvdpError error = cvvdp_create(
        width, height, fps, (FcvvdpDisplayModel)display_model,
        threads, params_ptr, &context);
    *out_context = NULL;
    if (error != CVVDP_OK) return map_error(error);
    FmetricsCvvdpCtx *wrapper = calloc(1, sizeof(*wrapper));
    if (!wrapper) {
        cvvdp_destroy(context);
        return FMETRICS_ERR_OUT_OF_MEMORY;
    }
    if (params_ptr == NULL)
        cvvdp_get_display_params((FcvvdpDisplayModel)display_model, &params);
    wrapper->backend = context;
    wrapper->hdr = params.is_hdr;
    *out_context = wrapper;
    return FMETRICS_OK;
}

void fmetrics_cvvdp_destroy(FmetricsCvvdpCtx *const context) {
    if (context == NULL) return;
    cvvdp_destroy(context->backend);
    free(context->pixels);
    free(context);
}

FmetricsErr fmetrics_cvvdp_process_frame(
    FmetricsCvvdpCtx *const context,
    const FmetricsImg *const reference,
    const FmetricsImg *const distorted,
    FmetricsCvvdpResult *const result)
{
    if (context == NULL) return FMETRICS_ERR_INVALID_ARGUMENT;
    FcvvdpImage ref_img, dis_img;
    const FcvvdpImage *ref_ptr, *dis_ptr;
    const FmetricsErr image_error = images(
        reference, distorted, &ref_img, &dis_img, &ref_ptr, &dis_ptr,
        context->hdr, &context->pixels, &context->capacity);
    if (image_error != FMETRICS_OK) return image_error;
    FcvvdpResult cvvdp_result;
    const FcvvdpError error = cvvdp_process_frame(
        context->backend, ref_ptr, dis_ptr,
        result == NULL ? NULL : &cvvdp_result);
    if (error == CVVDP_OK && result != NULL)
        *result = (FmetricsCvvdpResult){
            .jod = cvvdp_result.jod,
            .quality = cvvdp_result.quality,
        };
    return map_error(error);
}

FmetricsErr fmetrics_cvvdp_reset(FmetricsCvvdpCtx *const context) {
    return map_error(cvvdp_reset(context ? context->backend : NULL));
}

FmetricsErr fmetrics_cvvdp_cmp(
    const FmetricsImg *const reference,
    const FmetricsImg *const distorted,
    const FmetricsCvvdpDisplayModel display_model,
    const unsigned threads,
    const FmetricsCvvdpDisplayParams *const custom_params,
    FmetricsCvvdpResult *const result)
{
    FcvvdpDisplayParams params;
    const FcvvdpDisplayParams *params_ptr = NULL;
    if (custom_params != NULL &&
        display_model == FMETRICS_CVVDP_DISPLAY_CUSTOM)
    {
        params = display_params(custom_params);
        params_ptr = &params;
    } else {
        const FcvvdpError error = cvvdp_get_display_params(
            (FcvvdpDisplayModel)display_model, &params);
        if (error != CVVDP_OK) return map_error(error);
    }
    FcvvdpImage ref_img, dis_img;
    const FcvvdpImage *ref_ptr, *dis_ptr;
    float *pixels = NULL;
    size_t capacity = 0;
    const FmetricsErr image_error = images(
        reference, distorted, &ref_img, &dis_img, &ref_ptr, &dis_ptr,
        params.is_hdr, &pixels, &capacity);
    if (image_error != FMETRICS_OK) {
        free(pixels);
        return image_error;
    }
    FcvvdpResult cvvdp_result;
    const FcvvdpError error = cvvdp_compare_images(
        ref_ptr, dis_ptr, (FcvvdpDisplayModel)display_model, threads,
        params_ptr, result == NULL ? NULL : &cvvdp_result);
    if (error == CVVDP_OK && result != NULL)
        *result = (FmetricsCvvdpResult){
            .jod = cvvdp_result.jod,
            .quality = cvvdp_result.quality,
        };
    free(pixels);
    return map_error(error);
}

FmetricsErr fmetrics_cvvdp_get_display_params(
    const FmetricsCvvdpDisplayModel model,
    FmetricsCvvdpDisplayParams *const out_params)
{
    if (out_params == NULL)
        return map_error(cvvdp_get_display_params(
            (FcvvdpDisplayModel)model, NULL));
    FcvvdpDisplayParams params;
    const FcvvdpError error = cvvdp_get_display_params(
        (FcvvdpDisplayModel)model, &params);
    if (error == CVVDP_OK)
        *out_params = (FmetricsCvvdpDisplayParams){
            .resolution_width = params.resolution_width,
            .resolution_height = params.resolution_height,
            .viewing_distance_meters = params.viewing_distance_meters,
            .diagonal_size_inches = params.diagonal_size_inches,
            .max_luminance = params.max_luminance,
            .contrast = params.contrast,
            .ambient_light = params.ambient_light,
            .reflectivity = params.reflectivity,
            .is_hdr = params.is_hdr,
        };
    return map_error(error);
}

const char *fmetrics_cvvdp_version_str(void) {
    return cvvdp_version_string();
}

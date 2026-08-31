#include "cvvdp.h"
#include "fmetrics.h"
#include <limits.h>

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
        .colorspace = CVVDP_COLORSPACE_SRGB,
    };
}

static FmetricsErr validate_image(const FmetricsImg *const img) {
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
    if (img->colorspace != FMETRICS_COLORSPACE_SRGB)
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
    return FMETRICS_OK;
}

static FmetricsErr images(const FmetricsImg *const reference,
                          const FmetricsImg *const distorted,
                          FcvvdpImage *const ref_img,
                          FcvvdpImage *const dis_img,
                          const FcvvdpImage **const ref_ptr,
                          const FcvvdpImage **const dis_ptr)
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
    if (custom_params != NULL) {
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
    *out_context = (FmetricsCvvdpCtx *)context;
    return map_error(error);
}

void fmetrics_cvvdp_destroy(FmetricsCvvdpCtx *const context) {
    cvvdp_destroy((FcvvdpCtx *)context);
}

FmetricsErr fmetrics_cvvdp_process_frame(
    FmetricsCvvdpCtx *const context,
    const FmetricsImg *const reference,
    const FmetricsImg *const distorted,
    FmetricsCvvdpResult *const result)
{
    FcvvdpImage ref_img, dis_img;
    const FcvvdpImage *ref_ptr, *dis_ptr;
    const FmetricsErr image_error = images(
        reference, distorted, &ref_img, &dis_img, &ref_ptr, &dis_ptr);
    if (image_error != FMETRICS_OK) return image_error;
    FcvvdpResult cvvdp_result;
    const FcvvdpError error = cvvdp_process_frame(
        (FcvvdpCtx *)context, ref_ptr, dis_ptr,
        result == NULL ? NULL : &cvvdp_result);
    if (error == CVVDP_OK && result != NULL)
        *result = (FmetricsCvvdpResult){
            .jod = cvvdp_result.jod,
            .quality = cvvdp_result.quality,
        };
    return map_error(error);
}

FmetricsErr fmetrics_cvvdp_reset(FmetricsCvvdpCtx *const context) {
    return map_error(cvvdp_reset((FcvvdpCtx *)context));
}

FmetricsErr fmetrics_cvvdp_cmp(
    const FmetricsImg *const reference,
    const FmetricsImg *const distorted,
    const FmetricsCvvdpDisplayModel display_model,
    const unsigned threads,
    const FmetricsCvvdpDisplayParams *const custom_params,
    FmetricsCvvdpResult *const result)
{
    FcvvdpImage ref_img, dis_img;
    const FcvvdpImage *ref_ptr, *dis_ptr;
    const FmetricsErr image_error = images(
        reference, distorted, &ref_img, &dis_img, &ref_ptr, &dis_ptr);
    if (image_error != FMETRICS_OK) return image_error;
    FcvvdpDisplayParams params;
    const FcvvdpDisplayParams *params_ptr = NULL;
    if (custom_params != NULL) {
        params = display_params(custom_params);
        params_ptr = &params;
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

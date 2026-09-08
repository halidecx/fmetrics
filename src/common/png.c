#include "png.h"
#include <lcms2.h>
#include <spng.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}

static uint32_t chunk_crc(const uint8_t *p, size_t size) {
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < size; i++) {
        crc ^= p[i];
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

static double linearize(double v, unsigned tf) {
    if (tf == 16) {
        const double p = pow(v, 32.0 / 2523.0);
        return pow(fmax(p - 3424.0 / 4096.0, 0.0) /
                   (2413.0 / 128.0 - 2392.0 / 128.0 * p),
                   16384.0 / 2610.0) * (10000.0 / 203.0);
    }
    if (tf == 18) return v <= 0.5 ? v * v / 3.0 :
        (exp((v - 0.55991073) / 0.17883277) + 0.28466892) / 12.0;
    if (tf == 8) return v;
    if (tf == 4) return pow(v, 2.2);
    if (tf == 5) return pow(v, 2.8);
    if (tf == 1 || tf == 6 || tf == 14 || tf == 15)
        return v < 0.0812428583 ? v / 4.5 :
            pow((v + 0.0992968268) / 1.0992968268, 1.0 / 0.45);
    return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

int fmetrics_png_linear(const uint8_t *data, size_t size, float **rgb,
                        uint32_t *width, uint32_t *height, int *hdr)
{
    int status = 1;
    spng_ctx *ctx = spng_ctx_new(0);
    uint16_t *rgba = NULL;
    float *out = NULL;
    double *lut = NULL;
    cmsHPROFILE input = NULL, output = NULL;
    cmsHTRANSFORM transform = NULL;
    cmsToneCurve *curve = NULL;
    struct spng_ihdr ihdr;
    struct spng_iccp iccp;
    cmsCIExyY white = {0.3127, 0.3290, 1.0};
    cmsCIExyYTRIPLE primaries = {
        {0.64, 0.33, 1.0}, {0.30, 0.60, 1.0}, {0.15, 0.06, 1.0}
    };
    const cmsCIExyYTRIPLE srgb = primaries;
    unsigned tf = 13, cp = 1;
    int cicp = 0, full = 1;
    size_t decoded_size;
    if (!ctx || spng_set_png_buffer(ctx, data, size) ||
        spng_get_ihdr(ctx, &ihdr)) goto done;
    for (size_t pos = 8; pos <= size && size - pos >= 12;) {
        const uint32_t n = read32(data + pos);
        if (n > size - pos - 12) goto done;
        if (!memcmp(data + pos + 4, "IDAT", 4)) break;
        if (!memcmp(data + pos + 4, "cICP", 4)) {
            if (n != 4 || cicp ||
                chunk_crc(data + pos + 4, 8) != read32(data + pos + 12) ||
                data[pos + 10] ||
                data[pos + 11] > 1) goto done;
            cp = data[pos + 8];
            tf = data[pos + 9];
            full = data[pos + 11];
            cicp = 1;
        }
        pos += n + 12;
    }
    if (cicp) {
        if (cp == 9) primaries = (cmsCIExyYTRIPLE){
            {0.708, 0.292, 1}, {0.170, 0.797, 1}, {0.131, 0.046, 1}};
        else if (cp == 12) primaries = (cmsCIExyYTRIPLE){
            {0.680, 0.320, 1}, {0.265, 0.690, 1}, {0.150, 0.060, 1}};
        else if (cp != 1) goto done;
        if (tf == 18 && cp != 9) goto done;
        if (tf != 1 && tf != 4 && tf != 5 && tf != 6 && tf != 8 &&
            tf != 13 && tf != 14 && tf != 15 && tf != 16 && tf != 18)
            goto done;
    }
    if (spng_decoded_image_size(ctx, SPNG_FMT_RGBA16, &decoded_size))
        goto done;
    const size_t pixels = (size_t)ihdr.width * ihdr.height;
    if (pixels > SIZE_MAX / (3 * sizeof(float))) goto done;
    rgba = malloc(decoded_size);
    out = malloc(pixels * 3 * sizeof(float));
    if (!rgba || !out || spng_decode_image(ctx, rgba, decoded_size,
                                          SPNG_FMT_RGBA16, 0)) goto done;
    curve = cmsBuildGamma(NULL, 1.0);
    if (!curve) goto done;
    cmsToneCurve *curves[3] = {curve, curve, curve};
    output = cmsCreateRGBProfile(&white, &srgb, curves);
    if (!output) goto done;
    if (!cicp && !spng_get_iccp(ctx, &iccp)) {
        if (iccp.profile_len > UINT32_MAX) goto done;
        input = cmsOpenProfileFromMem(iccp.profile, iccp.profile_len);
        if (!input) goto done;
        const int gray = cmsGetColorSpace(input) == cmsSigGrayData;
        transform = cmsCreateTransform(input,
            gray ? TYPE_GRAY_16 : TYPE_RGBA_16, output, TYPE_RGB_FLT,
            INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_NOOPTIMIZE);
        if (!transform) goto done;
        if (gray) {
            for (size_t i = 0; i < pixels; i++)
                cmsDoTransform(transform, rgba + 4 * i, out + 3 * i, 1);
        } else {
            for (uint32_t y = 0; y < ihdr.height; y++)
                cmsDoTransform(transform, rgba + (size_t)y * ihdr.width * 4,
                    out + (size_t)y * ihdr.width * 3, ihdr.width);
        }
    } else {
        double gamma = 0.0;
        uint8_t intent;
        if (!cicp && spng_get_srgb(ctx, &intent)) {
            struct spng_chrm chrm;
            spng_get_gama(ctx, &gamma);
            if (!spng_get_chrm(ctx, &chrm)) {
                white = (cmsCIExyY){chrm.white_point_x,
                                   chrm.white_point_y, 1};
                primaries = (cmsCIExyYTRIPLE){
                    {chrm.red_x, chrm.red_y, 1},
                    {chrm.green_x, chrm.green_y, 1},
                    {chrm.blue_x, chrm.blue_y, 1}};
            }
        }
        input = cmsCreateRGBProfile(&white, &primaries, curves);
        if (!input) goto done;
        transform = cmsCreateTransform(input, TYPE_RGB_FLT, output,
            TYPE_RGB_FLT, INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_NOOPTIMIZE);
        if (!transform) goto done;
        lut = malloc(65536 * sizeof(double));
        if (!lut) goto done;
        const double maxval = ihdr.bit_depth == 16 ? 65535.0 : 255.0;
        const double scale = ihdr.bit_depth == 16 ? 256.0 : 1.0;
        for (unsigned i = 0; i < 65536; i++) {
            double v = i / 65535.0;
            if (!full) v = (v * maxval - 16 * scale) / (219 * scale);
            const double a = fabs(v);
            lut[i] = copysign(gamma > 0 ? pow(a, 1.0 / gamma) :
                             linearize(a, tf), v);
        }
        for (size_t i = 0; i < pixels; i++) {
            double v[3];
            for (unsigned ch = 0; ch < 3; ch++)
                v[ch] = lut[rgba[4 * i + ch]];
            double gain = 1.0;
            if (tf == 18) {
                const double y = fmax(0, 0.2627 * v[0] +
                    0.6780 * v[1] + 0.0593 * v[2]);
                gain = pow(y, 0.2) * (1000.0 / 203.0);
            }
            for (unsigned ch = 0; ch < 3; ch++) out[3 * i + ch] = v[ch] * gain;
        }
        for (uint32_t y = 0; y < ihdr.height; y++) {
            float *row = out + (size_t)y * ihdr.width * 3;
            cmsDoTransform(transform, row, row, ihdr.width);
        }
    }
    if (hdr) {
        *hdr = cicp && (tf == 16 || tf == 18);
        for (size_t i = 0; !cicp && !*hdr && i < pixels; i++)
            if (0.2126f * out[3 * i] + 0.7152f * out[3 * i + 1] +
                0.0722f * out[3 * i + 2] > 1.0001f) *hdr = 1;
    }
    *rgb = out;
    *width = ihdr.width;
    *height = ihdr.height;
    out = NULL;
    status = 0;
done:
    if (transform) cmsDeleteTransform(transform);
    if (input) cmsCloseProfile(input);
    if (output) cmsCloseProfile(output);
    if (curve) cmsFreeToneCurve(curve);
    free(lut);
    free(rgba);
    free(out);
    spng_ctx_free(ctx);
    return status;
}

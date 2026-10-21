#include "../common/mem.h"
#include "../common/color.h"
#include "../common/util.h"
#include <limits.h>

static const double weight[108] = {
    0.0,
    0.0007376606707406586,
    0.0,
    0.0,
    0.0007793481682867309,
    0.0,
    0.0,
    0.0004371155730107379,
    0.0,
    1.1041726426657346,
    0.00066284834129271,
    0.00015231632783718752,
    0.0,
    0.0016406437456599754,
    0.0,
    1.8422455520539298,
    11.441172603757666,
    0.0,
    0.0007989109436015163,
    0.000176816438078653,
    0.0,
    1.8787594979546387,
    10.94906990605142,
    0.0,
    0.0007289346991508072,
    0.9677937080626833,
    0.0,
    0.00014003424285435884,
    0.9981766977854967,
    0.00031949755934435053,
    0.0004550992113792063,
    0.0,
    0.0,
    0.0013648766163243398,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0,
    7.466890328078848,
    0.0,
    17.445833984131262,
    0.0006235601634041466,
    0.0,
    0.0,
    6.683678146179332,
    0.00037724407979611296,
    1.027889937768264,
    225.20515300849274,
    0.0,
    0.0,
    19.213238186143016,
    0.0011401524586618361,
    0.001237755635509985,
    176.39317598450694,
    0.0,
    0.0,
    24.43300999870476,
    0.28520802612117757,
    0.0004485436923833408,
    0.0,
    0.0,
    0.0,
    34.77906344483772,
    44.835625328877896,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0008680556573291698,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0005313191874358747,
    0.0,
    0.00016533814161379112,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0004179171803251336,
    0.0017290828234722833,
    0.0,
    0.0020827005846636437,
    0.0,
    0.0,
    8.826982764996862,
    23.19243343998926,
    0.0,
    95.1080498811086,
    0.9863978034400682,
    0.9834382792465353,
    0.0012286405048278493,
    171.2667255897307,
    0.9807858872435379,
    0.0,
    0.0,
    0.0,
    0.0005130064588990679,
    0.0,
    0.00010854057858411537,
};

typedef struct Plane {
    bool ssim, edge;
    float *ring[6];
    double sum[6];
} Plane;

typedef struct Scale {
    int w, h, y, level;
    float *row, *pair, *tmp, *error;
    Plane plane[3];
    struct Scale *next;
} Scale;

static inline float root(float x) {
    x = fmaxf(x, 1e-12f);
    uint32_t bits;
    memcpy(&bits, &x, sizeof(bits));
    bits = 0x54a21d2au - bits / 3;
    float r;
    memcpy(&r, &bits, sizeof(r));
    const float a = x / 3.0f;
    for (int i = 0; i < 2; i++) {
        const float r2 = r * r;
        r = 1.3333333333333333f * r - a * (r2 * r2);
    }
    const float r2 = r * r;
    r += (r - x * r2 * r2) / 3.0f;
    return x * r * r - 0.15595420055f;
}

static inline int reflect(int x, int n) {
    if (x < 0) return -x;
    if (x >= n) return 2 * n - 2 - x;
    return x;
}

static void horizontal(const float *restrict src, float *restrict dst,
                       int w)
{
    for (int x = 0; x < 4; x++) {
        dst[x] = 0.2665599883f * src[x];
        const float k[4] = {0.2134445459f, 0.1095860824f,
                           0.0360749699f, 0.0076144189f};
        for (int i = 1; i <= 4; i++)
            dst[x] += k[i - 1] *
                (src[reflect(x - i, w)] + src[reflect(x + i, w)]);
    }
    for (int x = 4; x < w - 4; x++)
        dst[x] = 0.2665599883f * src[x] +
            0.2134445459f * (src[x - 1] + src[x + 1]) +
            0.1095860824f * (src[x - 2] + src[x + 2]) +
            0.0360749699f * (src[x - 3] + src[x + 3]) +
            0.0076144189f * (src[x - 4] + src[x + 4]);
    for (int x = w - 4; x < w; x++) {
        dst[x] = 0.2665599883f * src[x];
        const float k[4] = {0.2134445459f, 0.1095860824f,
                           0.0360749699f, 0.0076144189f};
        for (int i = 1; i <= 4; i++)
            dst[x] += k[i - 1] *
                (src[reflect(x - i, w)] + src[reflect(x + i, w)]);
    }
}

static void vertical(const float *src, float *restrict dst, int w,
                     const int rows[9])
{
    const float *r0 = src + (size_t)rows[0] * w;
    const float *r1 = src + (size_t)rows[1] * w;
    const float *r2 = src + (size_t)rows[2] * w;
    const float *r3 = src + (size_t)rows[3] * w;
    const float *r4 = src + (size_t)rows[4] * w;
    const float *r5 = src + (size_t)rows[5] * w;
    const float *r6 = src + (size_t)rows[6] * w;
    const float *r7 = src + (size_t)rows[7] * w;
    const float *r8 = src + (size_t)rows[8] * w;
    for (int x = 0; x < w; x++)
        dst[x] = 0.2665599883f * r4[x] +
            0.2134445459f * (r3[x] + r5[x]) +
            0.1095860824f * (r2[x] + r6[x]) +
            0.0360749699f * (r1[x] + r7[x]) +
            0.0076144189f * (r0[x] + r8[x]);
}

static void measure(Scale *s, int y) {
    const int w = s->w;
    int rows[9];
    for (int k = 0; k < 9; k++)
        rows[k] = reflect(y + k - 4, s->h) % 9;
    float *m1 = s->tmp, *m2 = m1 + w;
    float *cross = m2 + w, *square = cross + w;
    for (int c = 0; c < 3; c++) {
        Plane *p = &s->plane[c];
        if (!p->ssim && !p->edge) continue;
        vertical(p->ring[0], m1, w, rows);
        vertical(p->ring[1], m2, w, rows);
        if (p->ssim) {
            vertical(p->ring[2], cross, w, rows);
            vertical(p->ring[3], square, w, rows);
        }
        const float *a = p->ring[4] + (size_t)(y % 9) * w;
        const float *b = p->ring[5] + (size_t)(y % 9) * w;
        const double *wt = weight + c * 36 + s->level * 6;
        float dsum = 0, d4sum = 0, asum = 0, a4sum = 0;
        float lsum = 0, l4sum = 0;
#if defined(__clang__)
#pragma clang loop vectorize(enable)
#endif
        for (int x = 0; x < w; x++) {
            float d = 0, artifact = 0, lost = 0;
            if (p->ssim) {
                const float diff = m1[x] - m2[x];
                const float covariance = 2.0f *
                    (cross[x] - m1[x] * m2[x]) + 0.0009f;
                const float variance = square[x] - diff * diff;
                d = fmaxf((variance + diff * diff * covariance) /
                          (covariance + variance), 0.0f);
                const float d2 = d * d;
                dsum += d;
                d4sum += d2 * d2;
            }
            if (p->edge) {
                const float n1 = fabsf(a[x] - m1[x]);
                const float n2 = fabsf(b[x] - m2[x]);
                const float delta = (n2 - n1) / (1.0f + n1);
                artifact = fmaxf(delta, 0.0f);
                lost = fmaxf(-delta, 0.0f);
                const float a2 = artifact * artifact;
                const float l2 = lost * lost;
                asum += artifact;
                a4sum += a2 * a2;
                lsum += lost;
                l4sum += l2 * l2;
            }
            if (s->error) s->error[(size_t)y * w + x] +=
                (float)((wt[0] + wt[3]) * d +
                        (wt[1] + wt[4]) * artifact +
                        (wt[2] + wt[5]) * lost);
        }
        p->sum[0] += dsum;
        p->sum[1] += asum;
        p->sum[2] += lsum;
        p->sum[3] += d4sum;
        p->sum[4] += a4sum;
        p->sum[5] += l4sum;
    }
}

#define XYB_ROW(X, B) \
    for (int x = 0; x < w; x++) { \
        const float cx = root(fmaxf(0.30f * r[x] + 0.622f * g[x] + \
                                   0.078f * b[x] + 0.0037930734f, 0)); \
        const float cy = root(fmaxf(0.23f * r[x] + 0.692f * g[x] + \
                                   0.078f * b[x] + 0.0037930734f, 0)); \
        const float yy = (cx + cy) * 0.5f; \
        if (X) ox[x] = (cx - cy) * 7.0f + 0.42f; \
        oy[x] = yy + 0.01f; \
        if (B) ob[x] = root(fmaxf(0.24342269f * r[x] + \
            0.20476745f * g[x] + 0.55180986f * b[x] + \
            0.0037930734f, 0)) - yy + 0.55f; \
    }

static void push(Scale *s) {
    const int w = s->w, y = s->y;
    if (s->next) {
        if (!(y & 1)) memcpy(s->pair, s->row, (size_t)w * 6 * 4);
        if ((y & 1) || y == s->h - 1) {
            const int nw = s->next->w;
            for (int c = 0; c < 6; c++) {
                const float *a = s->pair + (size_t)c * w;
                const float *b = s->row + (size_t)c * w;
                float *out = s->next->row + (size_t)c * nw;
                for (int x = 0; x < w / 2; x++)
                    out[x] = (a[2 * x] + a[2 * x + 1] +
                              b[2 * x] + b[2 * x + 1]) * 0.25f;
                if (w & 1) out[nw - 1] = (a[w - 1] + b[w - 1]) * 0.5f;
            }
            push(s->next);
        }
    }
    for (int img = 0; img < 2; img++) {
        const float *r = s->row + (size_t)img * 3 * w;
        const float *g = r + w, *b = g + w;
        float *restrict ox = s->plane[0].ring[4 + img];
        float *restrict oy = s->plane[1].ring[4 + img];
        float *restrict ob = s->plane[2].ring[4 + img];
        const size_t off = (size_t)(y % 9) * w;
        if (ox) ox += off;
        if (oy) oy += off;
        if (ob) ob += off;
        if (ob) { XYB_ROW(1, 1) }
        else if (ox) { XYB_ROW(1, 0) }
        else { XYB_ROW(0, 0) }
    }
    for (int c = 0; c < 3; c++) {
        Plane *p = &s->plane[c];
        if (!p->ssim && !p->edge) continue;
        const size_t off = (size_t)(y % 9) * w;
        float *a = p->ring[4] + off, *b = p->ring[5] + off;
        horizontal(a, p->ring[0] + off, w);
        horizontal(b, p->ring[1] + off, w);
        if (p->ssim) {
            for (int x = 0; x < w; x++) s->tmp[x] = a[x] * b[x];
            horizontal(s->tmp, p->ring[2] + off, w);
            for (int x = 0; x < w; x++) {
                const float v = a[x] - b[x];
                s->tmp[x] = v * v;
            }
            horizontal(s->tmp, p->ring[3] + off, w);
        }
    }
    if (y >= 4) measure(s, y - 4);
    if (++s->y == s->h)
        for (int i = s->h - 4; i < s->h; i++) measure(s, i);
}

static double score(double v) {
    v *= 0.9562382616834844;
    v = (6.248496625763138e-5 * v * v) * v +
        2.326765642916932 * v - 0.020884521182843837 * v * v;
    return v > 0 ? 100.0 - 10.0 * pow(v, 0.6276336467831387) : 100.0;
}

static float sample(const Scale *s, int x, int y, int w, int h) {
    const float fx = (float)x * s->w / w, fy = (float)y * s->h / h;
    const int x0 = (int)fx, y0 = (int)fy;
    const int x1 = x0 + (x0 + 1 < s->w);
    const int y1 = y0 + (y0 + 1 < s->h);
    const float dx = fx - x0, dy = fy - y0;
    const float a = s->error[(size_t)y0 * s->w + x0] * (1 - dx) +
        s->error[(size_t)y0 * s->w + x1] * dx;
    const float b = s->error[(size_t)y1 * s->w + x0] * (1 - dx) +
        s->error[(size_t)y1 * s->w + x1] * dx;
    return a * (1 - dy) + b * dy;
}

static void render_map(Scale *s, int count, uint32_t *out, int w, int h) {
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        float v = 0;
        for (int i = 0; i < count && i < 3; i++)
            v += sample(&s[i], x, y, w, h);
        const int index = (int)(255 * fmin(fmax(1 - score(v) / 100, 0), 1));
        out[(size_t)y * w + x] = TURBO_COLORMAP[index];
    }
}

static FmetricsErr compute(FmetricsWorkspace *ws, const FmetricsImg *ref,
                          const FmetricsImg *dist, double *result,
                          uint32_t *map)
{
    if (!ws || !ref || !dist || !result || !ref->data || !dist->data)
        return FMETRICS_ERR_INVALID_ARGUMENT;
    if (!ref->width || !ref->height || ref->width > INT_MAX - 16 ||
        ref->height > INT_MAX - 16) return FMETRICS_ERR_INVALID_ARGUMENT;
    if (ref->width != dist->width || ref->height != dist->height)
        return FMETRICS_ERR_DIMENSION_MISMATCH;
    FmetricsErr err = fmetrics_validate_rgb(ref);
    if (err != FMETRICS_OK) return err;
    err = fmetrics_validate_rgb(dist);
    if (err != FMETRICS_OK) return err;
    const int w = (int)ref->width, h = (int)ref->height;
    const size_t pixels = (size_t)w * h;
    if ((size_t)w > (SIZE_MAX - 4096) / 1024 ||
        (map && pixels > (SIZE_MAX - (size_t)w * 1024 - 4096) / 8))
        return FMETRICS_ERR_OUT_OF_MEMORY;
    const size_t size = (size_t)w * 1024 + 4096 + (map ? pixels * 8 : 0);
    if (!workspace_reserve(ws, size)) return FMETRICS_ERR_OUT_OF_MEMORY;
    Scale scales[5] = {0};
    int count = 0, sw = w, sh = h;
    for (int level = 0; level <= 4; level++) {
        if (level) {
            sw = (sw + 1) / 2;
            sh = (sh + 1) / 2;
        }
        if (sw < 8 || sh < 8) break;
        Scale *s = &scales[count++];
        s->w = sw;
        s->h = sh;
        s->level = level;
        s->row = scratch_alloc(&ws->scratch, (size_t)sw * 6 * 4);
        s->pair = scratch_alloc(&ws->scratch, (size_t)sw * 6 * 4);
        s->tmp = scratch_alloc(&ws->scratch, (size_t)sw * 4 * 4);
        if (map && level < 3) {
            s->error = scratch_alloc(&ws->scratch, (size_t)sw * sh * 4);
            memset(s->error, 0, (size_t)sw * sh * 4);
        }
        for (int c = 0; c < 3; c++) {
            Plane *p = &s->plane[c];
            const double *wt = weight + c * 36 + level * 6;
            p->ssim = wt[0] > 0.01 || wt[3] > 0.01;
            p->edge = wt[1] > 0.01 || wt[4] > 0.01 ||
                wt[2] > 0.01 || wt[5] > 0.01;
            if (!p->ssim && !p->edge) continue;
            for (int i = 0; i < 6; i++) {
                if (!p->ssim && (i == 2 || i == 3)) continue;
                p->ring[i] = scratch_alloc(&ws->scratch, (size_t)sw * 9 * 4);
            }
        }
        if (count > 1) scales[count - 2].next = s;
    }
    if (count) {
        float lut[256];
        for (int i = 0; i < 256; i++)
            lut[i] = fmetrics_srgb_linear(i / 255.0f);
        const FmetricsImg *images[2] = {ref, dist};
        for (int y = 0; y < h; y++) {
            for (int img = 0; img < 2; img++) {
                const FmetricsImg *in = images[img];
                const uint8_t *row = (const uint8_t *)in->data +
                    (size_t)y * in->stride;
                for (int c = 0; c < 3; c++) {
                    float *out = scales[0].row + (size_t)(img * 3 + c) * w;
                    if (fmetrics_is_linear(in)) {
                        for (int x = 0; x < w; x++)
                            memcpy(out + x, row + ((size_t)x * 3 + c) * 4, 4);
                    } else for (int x = 0; x < w; x++)
                        out[x] = lut[row[(size_t)x * 3 + c]];
                }
            }
            push(&scales[0]);
        }
    }

    double total = 0;
    for (int i = 0; i < count; i++) for (int c = 0; c < 3; c++) {
        const Scale *s = &scales[i];
        const double n = (double)s->w * s->h;
        for (int j = 0; j < 6; j++) {
            double v = s->plane[c].sum[j] / n;
            if (j >= 3) v = sqrt(sqrt(v));
            total += weight[c * 36 + s->level * 6 + j] * v;
        }
    }
    *result = score(total);
    if (map) render_map(scales, count, map, w, h);
    return FMETRICS_OK;
}

FmetricsErr fmetrics_ssimu2_cmp(FmetricsWorkspace *workspace,
                              const FmetricsImg *reference,
                              const FmetricsImg *distorted, double *result)
{
    return compute(workspace, reference, distorted, result, NULL);
}

FmetricsErr fmetrics_ssimu2_cmp_map(FmetricsWorkspace *workspace,
                                  const FmetricsImg *reference,
                                  const FmetricsImg *distorted, double *result,
                                  uint32_t *error_map)
{
    if (!error_map) return FMETRICS_ERR_INVALID_ARGUMENT;
    return compute(workspace, reference, distorted, result, error_map);
}

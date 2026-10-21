#include "src/fmetrics.h"
#include "src/common/mem.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>

extern size_t upstream_size(unsigned, unsigned);
extern double upstream_cmp(const float *, const float *, unsigned, unsigned,
                           float *);

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static int ready, go, mode, width, height, iterations;
static float *planar[2];
static FmetricsImg images[2];

typedef struct Result {
    double score;
    size_t bytes;
} Result;

static double seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void *worker(void *arg) {
    Result *r = arg;
    FmetricsWorkspace *ws = fmetrics_workspace_create();
    float *scratch = mode ? malloc(upstream_size(width, height) * 4) : NULL;
    for (int i = -3; i < iterations; i++) {
        if (!i) {
            pthread_mutex_lock(&mutex);
            ready++;
            pthread_cond_broadcast(&cond);
            while (!go) pthread_cond_wait(&cond, &mutex);
            pthread_mutex_unlock(&mutex);
        }
        if (mode) r->score = upstream_cmp(planar[0], planar[1], width,
                                         height, scratch);
        else if (fmetrics_ssimu2_cmp(ws, &images[0], &images[1], &r->score))
            abort();
    }
    r->bytes = mode ? upstream_size(width, height) * 4 : ws->scratch.size;
    free(scratch);
    fmetrics_workspace_destroy(ws);
    return NULL;
}

int main(int argc, char **argv) {
    width = argc > 1 ? atoi(argv[1]) : 1920;
    height = argc > 2 ? atoi(argv[2]) : 1080;
    const int threads = argc > 3 ? atoi(argv[3]) : 1;
    iterations = argc > 4 ? atoi(argv[4]) : 30;
    if (width < 8 || height < 8 || threads < 1 || iterations < 1) return 1;
    const size_t n = (size_t)width * height;
    for (int img = 0; img < 2; img++) {
        float *rgb = malloc(n * 12);
        planar[img] = malloc(n * 12);
        if (!rgb || !planar[img]) return 1;
        images[img] = (FmetricsImg){rgb, width, height, width * 12,
            FMETRICS_PIX_FMT_RGB_FLOAT, FMETRICS_COLORSPACE_LINEAR_SRGB, false};
        for (int y = 0; y < height; y++) for (int x = 0; x < width; x++)
            for (int c = 0; c < 3; c++) {
                const size_t i = (size_t)y * width + x;
                const float v = 0.4 + 0.2 * sin(x * 0.03 + c) +
                    0.15 * cos(y * 0.04 + c) + 0.05 * sin(x + y * 0.8) +
                    img * 0.01 * sin(x * 0.4 + y * 0.3 + c);
                rgb[i * 3 + c] = planar[img][c * n + i] = v;
            }
    }
    pthread_t *handles = malloc((size_t)threads * sizeof(*handles));
    Result *results = calloc((size_t)threads, sizeof(*results));
    if (!handles || !results) return 1;
    for (mode = 0; mode < 2; mode++) {
        ready = go = 0;
        for (int t = 0; t < threads; t++)
            if (pthread_create(handles + t, NULL, worker, results + t)) abort();
        pthread_mutex_lock(&mutex);
        while (ready != threads) pthread_cond_wait(&cond, &mutex);
        const double start = seconds();
        go = 1;
        pthread_cond_broadcast(&cond);
        pthread_mutex_unlock(&mutex);
        for (int t = 0; t < threads; t++) pthread_join(handles[t], NULL);
        const double elapsed = seconds() - start;
        printf("%s,%d,%d,%d,%.3f,%zu,%.9f\n",
               mode ? "vszip23" : "fmetrics", width, height, threads,
               threads * iterations / elapsed, results[0].bytes,
               results[0].score);
        fflush(stdout);
    }
    for (int img = 0; img < 2; img++) {
        free((void *)images[img].data);
        free(planar[img]);
    }
    free(handles);
    free(results);
}

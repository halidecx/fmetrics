#include <stddef.h>
#include <stdint.h>

int fmetrics_png_linear(const uint8_t *data, size_t size, float **rgb,
                        uint32_t *width, uint32_t *height, int *hdr);

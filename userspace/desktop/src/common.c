#include <zeroos/desktop/common.h>

void *zd_memset(void *destination, int value, size_t length) {
    uint8_t *bytes = (uint8_t *)destination;
    size_t index;
    for (index = 0; index < length; ++index)
        bytes[index] = (uint8_t)value;
    return destination;
}

void *zd_memcpy(void *destination, const void *source, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    size_t index;
    for (index = 0; index < length; ++index)
        out[index] = in[index];
    return destination;
}

void *zd_memmove(void *destination, const void *source, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    if (out == in || length == 0)
        return destination;
    if (out < in) {
        size_t index;
        for (index = 0; index < length; ++index)
            out[index] = in[index];
    } else {
        size_t index = length;
        while (index) {
            --index;
            out[index] = in[index];
        }
    }
    return destination;
}

int zd_memcmp(const void *a, const void *b, size_t length) {
    const uint8_t *left = (const uint8_t *)a;
    const uint8_t *right = (const uint8_t *)b;
    size_t index;
    for (index = 0; index < length; ++index) {
        if (left[index] != right[index])
            return left[index] < right[index] ? -1 : 1;
    }
    return 0;
}

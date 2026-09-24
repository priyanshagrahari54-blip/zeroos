#ifndef ZEROOS_DESKTOP_COMMON_H
#define ZEROOS_DESKTOP_COMMON_H

/* Shared types for the ZEROOS desktop platform core.
 *
 * The core is freestanding: every context owns fixed-capacity storage, no
 * heap allocation is performed, and no libc symbol is required beyond the
 * memcpy/memset family the compiler may emit for aggregate copies. The same
 * objects compile for host tests today and for Ring-3 desktop services once
 * the display/input kernel primitives publish the on-target session. */

#include <stdint.h>
#include <stddef.h>

#define ZD_ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

/* Signed error convention: 0 success, negative -ZD_E* failure. */
enum zd_error {
    ZD_OK = 0,
    ZD_EINVAL = 1,
    ZD_ENOSPC = 2,
    ZD_ENOENT = 3,
    ZD_EPERM = 4,
    ZD_EBUSY = 5,
    ZD_EAGAIN = 6,
    ZD_ECANCELED = 7,
    ZD_ENODEV = 8,
    ZD_ESTATE = 9,
    ZD_EOVERFLOW = 10
};

struct zd_rect {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
};

static inline int zd_rect_empty(struct zd_rect rect) {
    return rect.w <= 0 || rect.h <= 0;
}

static inline int zd_rect_contains(struct zd_rect rect, int32_t x, int32_t y) {
    return !zd_rect_empty(rect) && x >= rect.x && y >= rect.y &&
           x < rect.x + rect.w && y < rect.y + rect.h;
}

static inline int zd_rect_intersects(struct zd_rect a, struct zd_rect b) {
    return !zd_rect_empty(a) && !zd_rect_empty(b) &&
           a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

static inline struct zd_rect zd_rect_intersection(struct zd_rect a,
                                                  struct zd_rect b) {
    struct zd_rect out;
    int32_t x0 = a.x > b.x ? a.x : b.x;
    int32_t y0 = a.y > b.y ? a.y : b.y;
    int32_t x1 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    int32_t y1 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    out.x = x0;
    out.y = y0;
    out.w = x1 > x0 ? x1 - x0 : 0;
    out.h = y1 > y0 ? y1 - y0 : 0;
    return out;
}

static inline uint64_t zd_rect_area(struct zd_rect rect) {
    if (zd_rect_empty(rect))
        return 0;
    return (uint64_t)(uint32_t)rect.w * (uint64_t)(uint32_t)rect.h;
}

static inline uint32_t zd_min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static inline uint32_t zd_max_u32(uint32_t a, uint32_t b) {
    return a > b ? a : b;
}

static inline int32_t zd_clamp_i32(int32_t value, int32_t low, int32_t high) {
    if (low > high)
        return low;
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

/* Bounded string copy with explicit NUL termination. Destination capacity
 * includes the terminator; the result always fits. */
static inline void zd_str_copy(char *destination, size_t capacity,
                               const char *source) {
    size_t index = 0;
    if (!capacity)
        return;
    if (source) {
        while (source[index] && index + 1 < capacity) {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = '\0';
}

static inline int zd_str_equal(const char *a, const char *b) {
    if (!a || !b)
        return a == b;
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

/* ASCII case-insensitive compare; non-ASCII bytes compare literally. */
static inline char zd_lower_ascii(char value) {
    return (value >= 'A' && value <= 'Z') ? (char)(value - 'A' + 'a') : value;
}

static inline int zd_str_equal_ci(const char *a, const char *b) {
    if (!a || !b)
        return a == b;
    while (*a && zd_lower_ascii(*a) == zd_lower_ascii(*b)) {
        ++a;
        ++b;
    }
    return zd_lower_ascii(*a) == zd_lower_ascii(*b);
}

static inline size_t zd_str_length(const char *text) {
    size_t length = 0;
    if (!text)
        return 0;
    while (text[length])
        ++length;
    return length;
}

/* Case-insensitive substring probe used by search ranking and settings
 * search. Returns 1 when needle occurs in haystack, otherwise 0. */
static inline int zd_str_contains_ci(const char *haystack, const char *needle) {
    size_t index;
    size_t needle_length;
    if (!haystack || !needle || !*needle)
        return 0;
    needle_length = zd_str_length(needle);
    for (index = 0; haystack[index]; ++index) {
        size_t inner = 0;
        while (inner < needle_length && haystack[index + inner] &&
               zd_lower_ascii(haystack[index + inner]) ==
                   zd_lower_ascii(needle[inner]))
            ++inner;
        if (inner == needle_length)
            return 1;
    }
    return 0;
}

static inline uint64_t zd_hash64(const char *text) {
    /* FNV-1a 64-bit; deterministic across builds for index identity. */
    uint64_t hash = 1469598103934665603ULL;
    if (!text)
        return hash;
    while (*text) {
        hash ^= (uint8_t)(*text++);
        hash *= 1099511628211ULL;
    }
    return hash;
}

void *zd_memset(void *destination, int value, size_t length);
void *zd_memcpy(void *destination, const void *source, size_t length);
void *zd_memmove(void *destination, const void *source, size_t length);
int zd_memcmp(const void *a, const void *b, size_t length);

#endif

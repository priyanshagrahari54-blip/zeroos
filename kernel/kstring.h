#ifndef ZEROOS_KSTRING_H
#define ZEROOS_KSTRING_H
#include "types.h"

/* GCC may emit calls to memcpy/memset/memmove/memcmp even under
 * -ffreestanding -fno-builtin (struct copies, zero-initialisers), so the
 * kernel provides the standard symbols. */
typedef unsigned long zeroos_size_t;
void *memcpy(void *destination, const void *source, zeroos_size_t length);
void *memmove(void *destination, const void *source, zeroos_size_t length);
void *memset(void *destination, int value, zeroos_size_t length);
int memcmp(const void *left, const void *right, zeroos_size_t length);

uint64_t kstrnlen(const char *text, uint64_t limit);
int kstrneq(const char *left, const char *right, uint64_t limit);

/* Bounded formatter: %s %c %d %u %x (with l/ll modifiers ignored-width 64),
 * %p. Always NUL-terminates; returns bytes written excluding NUL. */
uint64_t ksnprintf(char *buffer, uint64_t size, const char *format, ...)
    __attribute__((format(printf,3,4)));
/* Emit one complete serial line (prefix "ZEROOS: " is the caller's). The
 * formatted line is bounded to 256 bytes; storage code never formats key
 * material or file contents through this path. */
void klog(const char *format, ...) __attribute__((format(printf,1,2)));

#endif

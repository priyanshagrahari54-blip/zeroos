#ifndef ZEROOS_DESKTOP_TEST_HARNESS_H
#define ZEROOS_DESKTOP_TEST_HARNESS_H

/* Minimal assertion harness for the desktop platform core. Failures print
 * file:line and the failing expression; the process exit code reflects
 * the aggregate result so CI fails closed. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int zd_test_failures;
extern int zd_test_checks;
extern const char *zd_test_current;

#define ZD_CHECK(expr)                                                     \
    do {                                                                   \
        ++zd_test_checks;                                                  \
        if (!(expr)) {                                                     \
            ++zd_test_failures;                                            \
            fprintf(stderr, "FAIL %s:%d [%s] %s\n", __FILE__, __LINE__,    \
                    zd_test_current, #expr);                               \
        }                                                                  \
    } while (0)

#define ZD_CHECK_EQ(a, b) ZD_CHECK((a) == (b))
#define ZD_CHECK_NE(a, b) ZD_CHECK((a) != (b))
#define ZD_CHECK_OK(expr) ZD_CHECK((expr) == 0)
#define ZD_CHECK_ERR(expr, err) ZD_CHECK((expr) == -(err))

#define ZD_RUN(fn)                                                         \
    do {                                                                   \
        zd_test_current = #fn;                                             \
        printf("  RUN  %s\n", #fn);                                        \
        fn();                                                              \
    } while (0)

#endif

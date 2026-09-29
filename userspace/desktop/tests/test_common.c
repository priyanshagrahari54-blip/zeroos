/* Shared primitives (common.h): the byte and string helpers every
 * freestanding desktop core is built on. They are small enough to be
 * trusted by inspection, which is exactly why they are asserted here --
 * a defect in one of them would look like a defect in whichever core
 * happened to call it. */
#include <zeroos/desktop/desktop.h>
#include "test_harness.h"

static void test_memcmp_contract(void) {
    /* Equal blocks compare equal whatever their length. */
    ZD_CHECK_EQ(zd_memcmp("abc", "abc", 3), 0);
    ZD_CHECK_EQ(zd_memcmp("abc", "abc", 0), 0);
    ZD_CHECK_EQ(zd_memcmp("", "", 0), 0);
    /* The sign follows the first differing byte, not the sum of them. */
    ZD_CHECK_EQ(zd_memcmp("abc", "abd", 3), -1);
    ZD_CHECK_EQ(zd_memcmp("abd", "abc", 3), 1);
    ZD_CHECK(zd_memcmp("zab", "abc", 3) > 0);
    ZD_CHECK(zd_memcmp("abc", "zab", 3) < 0);
    /* A difference past a shared prefix is still found, and an embedded NUL
     * is data like any other byte. */
    ZD_CHECK_EQ(zd_memcmp("a\0b", "a\0c", 3), -1);
    ZD_CHECK_EQ(zd_memcmp("a\0c", "a\0b", 3), 1);
    ZD_CHECK_EQ(zd_memcmp("abc", "ab\0", 3), 1);
}

static void test_string_helpers(void) {
    char buffer[8];

    /* Copy saturates: it always terminates and never writes past capacity. */
    zd_str_copy(buffer, sizeof(buffer), "abcdefghijklmnop");
    ZD_CHECK_EQ((uint32_t)zd_str_length(buffer), sizeof(buffer) - 1);
    ZD_CHECK(zd_str_equal(buffer, "abcdefg"));
    zd_str_copy(buffer, sizeof(buffer), "ab");
    ZD_CHECK(zd_str_equal(buffer, "ab"));
    zd_str_copy(buffer, sizeof(buffer), 0);
    ZD_CHECK_EQ((uint32_t)zd_str_length(buffer), 0U);
    zd_str_copy(buffer, sizeof(buffer), "keep");
    /* No capacity means no write at all: the buffer is left alone rather
     * than being truncated to an empty string. */
    zd_str_copy(buffer, 0, "ab");
    ZD_CHECK(zd_str_equal(buffer, "keep"));

    ZD_CHECK_EQ(zd_str_length(""), 0U);
    ZD_CHECK_EQ(zd_str_length("four"), 4U);
    ZD_CHECK(zd_str_equal("same", "same"));
    ZD_CHECK(!zd_str_equal("same", "sam"));
    ZD_CHECK(!zd_str_equal("same", "sane"));

    /* Bounded comparison: the limit is a length, not a hint. */
    ZD_CHECK_EQ(zd_str_length("abcd"), 4U);
    ZD_CHECK(zd_memcmp("abcd", "abcf", 3) == 0);
}

void zd_test_common_suite(void) {
    printf(" suite: shared primitives\n");
    ZD_RUN(test_memcmp_contract);
    ZD_RUN(test_string_helpers);
}

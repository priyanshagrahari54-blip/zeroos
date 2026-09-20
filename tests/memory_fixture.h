#ifndef ZEROOS_TEST_MEMORY_FIXTURE_H
#define ZEROOS_TEST_MEMORY_FIXTURE_H
#include "memory.h"
/* Native tests parse a real Multiboot mmap tag; physical pages are metadata
 * only here and must not be dereferenced by the host process. */
char __kernel_start, __kernel_end;
static void test_memory_init(void) {
    struct {
        uint32_t size, reserved;
        uint32_t type, tag_size, entry_size, version;
        uint64_t address, length;
        uint32_t available, padding;
        uint32_t end_type, end_size;
    } map={56,0,6,40,24,0,0,32ULL*1024*1024,1,0,0,8};
    memory_init((uint64_t)&map);
}
#endif

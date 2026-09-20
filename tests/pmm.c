#include <assert.h>
#include <stdio.h>
#include "memory_fixture.h"
static void *pages[9000];
struct fixture_entry { uint64_t start,length; uint32_t type,pad; };
struct fixture_map {
    uint32_t size,reserved,type,tag_size,stride,version;
    struct fixture_entry entries[3];
    uint32_t end_type,end_size;
};
static void map_boundaries(void) {
    struct fixture_map good={104,0,6,88,24,0,
        {{0x100001,0x4ffe,1,0},{0x102fff,2,2,0},{0x100000,0x1000,1,0}},0,8};
    for (unsigned order=0;order<2;++order) {
        memory_init((uint64_t)&good);
        assert(memory_free_pages()==2);
        assert(page_alloc_at(0x100000) && page_alloc_at(0x101000));
        assert(!page_alloc_at(0x102000) && !page_alloc_at(0x103000) && !page_alloc_at(0x104000));
        struct fixture_entry swap=good.entries[0];
        good.entries[0]=good.entries[1]; good.entries[1]=swap;
    }
    for (unsigned test=0;test<11;++test) {
        struct fixture_map bad=good;
        switch (test) {
        case 0: bad.size=8; break;
        case 1: bad.size=~0U; break;
        case 2: bad.tag_size=7; break;
        case 3: bad.tag_size=200; break;
        case 4: bad.stride=0; break;
        case 5: bad.stride=23; break;
        case 6: bad.tag_size=87; break;
        case 7: bad.version=1; break;
        case 8: bad.end_type=1; break;
        case 9: bad.entries[0].length=~0ULL; break;
        case 10: bad.tag_size=96; bad.stride=40; break;
        }
        memory_init((uint64_t)&bad);
        assert(memory_free_pages()==0 && !page_alloc());
    }
    struct fixture_map capped={104,0,6,88,24,0,
        {{511ULL*1024*1024,257ULL*1024*1024,1,0},{0,0,2,0},{0,0,2,0}},0,8};
    memory_init((uint64_t)&capped);
    assert(memory_free_pages()==256 && page_alloc_at(511ULL*1024*1024));
    assert(!page_alloc_at(512ULL*1024*1024));
    memory_init(0); assert(memory_free_pages()==0);
}
int main(void) {
    map_boundaries();
    test_memory_init();
    uint64_t before=memory_free_pages();
    page_free(0); page_free((void *)0x1000); page_free((void *)0x20000000);
    assert(memory_free_pages()==before);
    assert(!page_alloc_at(0) && !page_alloc_at(0x1001));
    assert(memory_claim_page(0)==-1);
    memory_test_fail_after(0);
    assert(!page_alloc() && memory_free_pages()==before);
    memory_test_fail_after(-1);
    unsigned count=0;
    while (count<9000 && (pages[count]=page_alloc())) {
        assert(memory_page_is_allocated((uint64_t)pages[count]));
        ++count;
    }
    assert(count==before && memory_free_pages()==0 && !page_alloc());
    assert(count>0);
    assert(memory_claim_page((uint64_t)pages[0])==0);
    page_free(pages[0]);
    assert(memory_free_pages()==0);
    memory_unclaim_page((uint64_t)pages[0]);
    for (unsigned i=0;i<count;++i) page_free(pages[i]);
    assert(memory_free_pages()==before);
    page_free(pages[0]);
    assert(memory_free_pages()==before);
    assert(memory_find_free_run(0)==0);
    assert(memory_find_free_run(~0ULL)==~0ULL);
    assert(memory_find_free_run(64)==256);
    puts("PASS: PMM malformed/overlapping map, rounding/cap, reservation/claim/exhaustion/double-free/failure boundaries");
}

#include <assert.h>
#include <stdio.h>
#include "memory_fixture.h"
static void *pages[9000];
int main(void) {
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
    puts("PASS: PMM reservation/claim/exhaustion/double-free/failure boundaries");
}

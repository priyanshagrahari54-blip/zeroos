#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "vmm.h"
#include "memory_fixture.h"

static unsigned live, fail_alloc;
void *kmalloc(uint64_t size) {
    if (fail_alloc) return 0;
    void *p=malloc(size); assert(p); ++live; return p;
}
int kfree(void *p) { assert(p && live); --live; free(p); return 0; }

int main(void) {
    test_memory_init();
    assert(page_alloc_at(0x100000));
    assert(page_alloc_at(0x200000));
    assert(page_alloc_at(0x300000));
    uint64_t root[512]={0};
    struct vmm_space space={.root=root}, other={.root=root};
    for (unsigned i=0;i<1000;++i) {
        assert(vmm_space_own_page(&space,0x100000)==0);
        assert(vmm_space_own_page(&space,0x200000)==0);
        assert(vmm_space_own_page(&space,0x300000)==0);
        assert(vmm_space_own_page(&other,0x100000)==-1);
        uint64_t before=memory_free_pages();
        page_free((void *)0x100000); /* claimed pages cannot be freed */
        page_free((void *)0);        /* reserved pages cannot be freed */
        assert(memory_free_pages()==before);
        assert(live==3 && space.owned_page_count==3);
        assert(vmm_space_own_page(&space,0x200000)==-1);
        assert(vmm_space_own_page(&space,0x100001)==-1);
        assert(vmm_space_release_page(&space,0x200000)==0); /* middle */
        assert(space.owned_pages->physical==0x300000);
        assert(space.owned_pages->next->physical==0x100000);
        assert(vmm_space_release_page(&space,0x300000)==0); /* head */
        assert(space.owned_pages->physical==0x100000);
        assert(vmm_space_release_page(&space,0x100000)==0); /* last */
        assert(vmm_space_release_page(&space,0x100000)==-1);
        assert(!space.owned_pages && !space.owned_page_count && !live);
    }
    assert(vmm_space_own_page(&space,0)==-1);
    assert(vmm_space_own_page(&space,0x400000)==-1); /* never allocated */
    fail_alloc=1;
    assert(vmm_space_own_page(&space,0x100000)==-1);
    assert(!space.owned_pages && !space.owned_page_count && !live);
    fail_alloc=0;
    assert(vmm_space_own_page(&other,0x100000)==0); /* failed allocation unwound claim */
    assert(vmm_space_release_page(&other,0x100000)==0);
    uint16_t pcids[31];
    for (unsigned round=0;round<100;++round) {
        for (unsigned i=0;i<31;++i) { pcids[i]=vmm_pcid_alloc(); assert(pcids[i]==i+1); }
        assert(!vmm_pcid_alloc() && vmm_pcid_in_use()==31);
        for (unsigned i=0;i<31;++i) vmm_pcid_free(pcids[i]);
        assert(vmm_pcid_in_use()==0);
    }
    puts("PASS: ownership list head/middle/tail removal and allocation failure");
}

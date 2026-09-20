#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "vmm.h"

static unsigned live, fail_alloc;
void *kmalloc(uint64_t size) {
    if (fail_alloc) return 0;
    void *p=malloc(size); assert(p); ++live; return p;
}
int kfree(void *p) { assert(p && live); --live; free(p); return 0; }

int main(void) {
    uint64_t root[512]={0};
    struct vmm_space space={.root=root};
    for (unsigned i=0;i<1000;++i) {
        assert(vmm_space_own_page(&space,0x1000)==0);
        assert(vmm_space_own_page(&space,0x2000)==0);
        assert(vmm_space_own_page(&space,0x3000)==0);
        assert(live==3 && space.owned_page_count==3);
        assert(vmm_space_own_page(&space,0x2000)==-1);
        assert(vmm_space_own_page(&space,0x1001)==-1);
        assert(vmm_space_release_page(&space,0x2000)==0); /* middle */
        assert(space.owned_pages->physical==0x3000);
        assert(space.owned_pages->next->physical==0x1000);
        assert(vmm_space_release_page(&space,0x3000)==0); /* head */
        assert(space.owned_pages->physical==0x1000);
        assert(vmm_space_release_page(&space,0x1000)==0); /* last */
        assert(vmm_space_release_page(&space,0x1000)==-1);
        assert(!space.owned_pages && !space.owned_page_count && !live);
    }
    fail_alloc=1;
    assert(vmm_space_own_page(&space,0x1000)==-1);
    assert(!space.owned_pages && !space.owned_page_count && !live);
    puts("PASS: ownership list head/middle/tail removal and allocation failure");
}

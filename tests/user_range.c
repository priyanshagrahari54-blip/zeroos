#include <assert.h>
#include <stdio.h>
#include "vmm.h"

static uint64_t root[512] __attribute__((aligned(4096)));
static uint64_t pdpt[512] __attribute__((aligned(4096)));
static uint64_t pd[512] __attribute__((aligned(4096)));
static uint64_t pt[512] __attribute__((aligned(4096)));
static struct vmm_space space;
#define VA 0x00007f0000002000ULL

int main(void) {
    root[254]=(uint64_t)pdpt|7;
    pdpt[0]=(uint64_t)pd|7;
    pd[0]=(uint64_t)pt|7;
    pt[2]=0x200000|7|VMM_NO_EXECUTE;
    pt[3]=0x201000|7|VMM_NO_EXECUTE;
    space.root=root;
    assert(vmm_space_is_user_range(&space,VA,8192,1));
    assert(vmm_space_is_user_range(&space,VA+4095,2,0));
    assert(!vmm_space_is_user_range(&space,VA,8193,0));
    assert(!vmm_space_is_user_range(&space,VA,0,0));
    assert(!vmm_space_is_user_range(&space,VA,~0ULL,0));
    assert(!vmm_space_is_user_range(&space,0,1,0));
    assert(!vmm_space_is_user_range(&space,0x800000000000ULL,1,0));
    assert(!vmm_space_is_user_range(&space,VA,(1ULL<<39),0));
    assert(!vmm_space_is_user_range(0,VA,1,0));
    uint64_t *entries[]={&root[254],&pdpt[0],&pd[0],&pt[2],&pt[3]};
    for (unsigned i=0;i<5;++i) {
        uint64_t saved=*entries[i];
        *entries[i]=saved&~VMM_USER;
        assert(!vmm_space_is_user_range(&space,VA,8192,0));
        *entries[i]=saved&~VMM_PRESENT;
        assert(!vmm_space_is_user_range(&space,VA,8192,0));
        *entries[i]=saved&~VMM_WRITABLE;
        assert(vmm_space_is_user_range(&space,VA,8192,0));
        assert(!vmm_space_is_user_range(&space,VA,8192,1));
        *entries[i]=saved;
    }
    /* Unsupported upper-level huge entries must not be dereferenced as tables. */
    pdpt[0]|=0x80;
    assert(!vmm_space_is_user_range(&space,VA,1,0));
    pdpt[0]&=~0x80ULL;
    for (unsigned i=0;i<10000;++i)
        assert(vmm_space_is_user_range(&space,VA+(i%4096),4096,1));
    puts("PASS: whole-range user permission/overflow/boundary/stress checks");
}

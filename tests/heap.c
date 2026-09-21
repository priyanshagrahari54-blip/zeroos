#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "heap.h"
#include "memory.h"
#include "sync.h"

/* Real heap implementation; only physical backing and privileged IRQ edges
 * are replaced. The fixture deliberately exercises the 1 MiB fallback. */
static unsigned char arena[1024*1024] __attribute__((aligned(4096)));
static unsigned char pages[256];
static int fail_page=-1, live_pages;
uint64_t memory_find_free_run(uint64_t n) {
    return n==256 ? (uint64_t)arena/4096 : ~0ULL;
}
void *page_alloc_at(uint64_t address) {
    if (fail_page==0) return 0;
    if (fail_page>0) --fail_page;
    assert(address>=(uint64_t)arena && address<(uint64_t)(arena+sizeof(arena)));
    unsigned i=(unsigned)((address-(uint64_t)arena)/4096);
    assert(!pages[i]); pages[i]=1; ++live_pages;
    return (void *)address;
}
void page_free(void *p) {
    unsigned i=(unsigned)(((uint64_t)p-(uint64_t)arena)/4096);
    assert(i<256 && pages[i]); pages[i]=0; --live_pages;
}
void spinlock_init(struct spinlock *p) { p->value=0; }
uint64_t spin_lock_irqsave(struct spinlock *p) { assert(!p->value); p->value=1; return 0; }
void spin_unlock_irqrestore(struct spinlock *p,uint64_t f) { (void)f; assert(p->value); p->value=0; }
void serial_write_public(const char *s) {
    if (!strncmp(s,"ZEROOS PANIC:",13)) _Exit(77);
}
void serial_write_u64_public(uint64_t n) { (void)n; }

static void corruption_test(unsigned which) {
    pid_t child=fork(); assert(child>=0);
    if (!child) {
        unsigned char *p=kmalloc(64); assert(p);
        uint64_t *header=(uint64_t *)(p-32);
        if (which==0) header[0]=0;
        if (which==1) header[0]=~0ULL-15;
        if (which==2) header[2]^=1;
        assert(heap_validate()==-1);
        if (which==2) (void)kfree(p); else (void)kmalloc(64);
        _Exit(1);
    }
    int status; assert(waitpid(child,&status,0)==child);
    assert(WIFEXITED(status) && WEXITSTATUS(status)==77);
    assert(heap_validate()==0);
}
int main(void) {
    assert(!kmalloc(1) && heap_validate()==-1);
    for (int i=0;i<256;++i) {
        fail_page=i;
        assert(heap_init()==-1 && live_pages==0);
    }
    fail_page=-1;
    assert(heap_init()==0 && heap_init()==0 && live_pages==256);
    uint64_t capacity=heap_capacity_bytes();
    assert(capacity==sizeof(arena)-32 && heap_used_bytes()==0);
    for (unsigned i=0;i<3;++i) corruption_test(i);
    void *a=kmalloc(1),*b=kmalloc(31),*c=kmalloc(1000);
    assert(a && b && c && !(((uint64_t)a|(uint64_t)b|(uint64_t)c)&15));
    assert(kfree((char *)c+16)==-1 && kfree((char *)c+3)==-1);
    assert(kfree(a)==0 && kfree(b)==0 && kfree(c)==0);
    assert(kfree(a)==-1 && kfree(b)==-1 && kfree(c)==-1);
    assert(kfree(0)==-1 && kfree((void *)1)==-1);
    assert(heap_test_forged_free()==0);
    a=kmalloc(capacity); assert(a && !kmalloc(1));
    memset(a,0xA5,capacity); assert(kfree(a)==0);
    assert(!kmalloc(capacity+1) && !kmalloc(~0ULL) && !kcalloc(~0ULL,2));
    assert(!kcalloc(0,1) && !kcalloc(1,0));
    a=kcalloc(128,8); assert(a);
    for (unsigned i=0;i<1024;++i) assert(!((unsigned char *)a)[i]);
    assert(kfree(a)==0);
    heap_test_fail_after(0); assert(!kmalloc(1)); heap_test_fail_after(-1);
    void *held[1024];
    for (unsigned i=0;i<1024;++i) { held[i]=kmalloc(992); assert(held[i]); }
    assert(!kmalloc(1));
    for (unsigned i=0;i<1024;++i) assert(kfree(held[i])==0);
    memset(held,0,sizeof(held));
    uint64_t seed=1;
    for (unsigned i=0;i<10000;++i) {
        seed=seed*6364136223846793005ULL+1;
        unsigned slot=(seed>>32)%128;
        if (held[slot]) { assert(kfree(held[slot])==0); held[slot]=0; }
        else { held[slot]=kmalloc((seed%512)+1); assert(held[slot]); }
        assert(heap_validate()==0);
    }
    for (unsigned i=0;i<128;++i) if (held[i]) assert(kfree(held[i])==0);
    assert(heap_used_bytes()==0 && heap_validate()==0);
    puts("PASS: heap initialization rollback, boundaries, coalesced/forged frees, corruption, exhaustion and stress");
}

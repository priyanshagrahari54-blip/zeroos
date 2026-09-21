#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "syscall.h"
#include "process.h"
#include "task.h"

static uint64_t root[512] __attribute__((aligned(4096)));
static uint64_t pdpt[512] __attribute__((aligned(4096)));
static uint64_t pd[512] __attribute__((aligned(4096)));
static uint64_t pt[512] __attribute__((aligned(4096)));
static unsigned char page[4096] __attribute__((aligned(4096)));
static struct process process;
static struct task thread;
static char captured[512];
static uint64_t written;
static unsigned yielded, faulted;
static jmp_buf contained;
#define VA 0x00007f0000002000ULL

struct task *task_current(void) { return &thread; }
void task_yield(void) { ++yielded; }
void process_exit(uint64_t code) { (void)code; assert(0); }
void process_user_fault(uint64_t rip, uint64_t vector) {
    (void)rip; assert(vector==13); ++faulted;
}
void task_exit(void) { longjmp(contained,1); }
void serial_write_public(const char *text) {
    assert(strcmp(text,"ZEROOS: invalid syscall return state contained.\n")==0);
}
void serial_write_bytes_public(const char *text, uint64_t length) {
    assert(length<=512); written=length; memcpy(captured,text,length);
}

static struct syscall_frame call(uint64_t number, uint64_t address, uint64_t length) {
    struct syscall_frame f={.rax=number,.rdi=1,.rsi=address,.rdx=length,
        .rcx=VA,.r11=~0ULL,.user_rsp=VA+4095};
    syscall_dispatch(&f);
    assert(f.r11==0xad7); /* arithmetic flags + IF + reserved bit 1 only */
    return f;
}

int main(void) {
    root[254]=(uint64_t)pdpt|7; pdpt[0]=(uint64_t)pd|7;
    pd[0]=(uint64_t)pt|7; pt[2]=(uint64_t)page|7|VMM_NO_EXECUTE;
    process.space.root=root; process.pid=42;
    thread.process=&process; thread.id=99;
    memset(page,'Q',sizeof(page)); page[19]=0; /* embedded NUL is a byte, not an end marker */
    assert(call(ZEROOS_SYSCALL_WRITE,VA,512).rax==512);
    assert(written==512 && memcmp(captured,page,512)==0);
    assert(call(ZEROOS_SYSCALL_WRITE,VA,513).rax==~0ULL);
    assert(call(ZEROOS_SYSCALL_WRITE,VA,~0ULL).rax==~0ULL);
    assert(call(ZEROOS_SYSCALL_WRITE,0x1000,8).rax==~0ULL);
    assert(call(ZEROOS_SYSCALL_WRITE,VA+4095,2).rax==~0ULL);
    assert(call(ZEROOS_SYSCALL_WRITE,VA+4095,1).rax==1);
    assert(call(ZEROOS_SYSCALL_WRITE,0,0).rax==0);
    assert(call(ZEROOS_SYSCALL_GETPID,0,0).rax==42);
    assert(call(ZEROOS_SYSCALL_GETTID,0,0).rax==99);
    assert(call(ZEROOS_SYSCALL_YIELD,0,0).rax==0 && yielded==1);
    assert(call(999,0,0).rax==~0ULL);
    char dest[2]={42,42};
    assert(copy_from_user(dest,&process.space,VA+4095,2)==-1);
    assert(dest[0]==42 && dest[1]==42); /* reject whole range before copying */
    if (!setjmp(contained)) {
        struct syscall_frame bad={.rcx=0x800000000000ULL,.user_rsp=VA,.rax=3};
        syscall_dispatch(&bad);
        assert(0);
    }
    assert(faulted==1);
    if (!setjmp(contained)) {
        struct syscall_frame bad={.rcx=VA,.user_rsp=0x1000,.rax=3};
        syscall_dispatch(&bad);
        assert(0);
    }
    assert(faulted==2);
    puts("PASS: syscall dispatch, bounded binary writes, invalid return containment");
}

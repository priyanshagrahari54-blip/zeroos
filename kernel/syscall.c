#include "syscall.h"
#include "gdt.h"
#include "process.h"
#include "task.h"
#include "scheduler.h"

extern void syscall_entry(void);
extern void serial_write_public(const char *text);

#define MSR_EFER   0xC0000080
#define MSR_STAR   0xC0000081
#define MSR_LSTAR  0xC0000082
#define MSR_SFMASK 0xC0000084

#define RFLAGS_IF  0x100ULL
#define RFLAGS_DF  0x200ULL
#define RFLAGS_AC  0x10000ULL

static inline void wrmsr(uint32_t index, uint64_t value) {
    __asm__ volatile ("wrmsr"
                      :
                      : "c"(index),
                        "a"((uint32_t)(value & 0xffffffffULL)),
                        "d"((uint32_t)(value >> 32)));
}

static inline uint64_t rdmsr(uint32_t index) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(index));
    return ((uint64_t)hi << 32) | lo;
}

/*
 * Called from the SYSCALL trampoline when the entry state is invalid:
 * SYSCALL executed in ring 0, or with an RSP outside the user region.
 * After SYSCALL the CS is always the kernel selector from STAR, so the
 * ring of the caller cannot be read from CS; the user-RSP check in the
 * trampoline is the authoritative ring-3 proof.
 */
void syscall_entry_abort(void) {
    serial_write_public("ZEROOS PANIC: syscall entry invalid (kernel-mode SYSCALL or bad user RSP).\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

void syscall_init(void) {
    /*
     * EFER.SCE enables SYSCALL/SYSRET. LME is already set by the boot code;
     * preserve every other EFER bit.
     */
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | 1ULL);

    /*
     * STAR layout for this GDT:
     *   bits 32-47: kernel code selector (0x08) -> SYSCALL CS
     *   bits 48-63: SYSRET base (0x10)          -> CS 0x23, SS 0x1B (RPL3)
     */
    wrmsr(MSR_STAR,
          ((uint64_t)ZEROOS_SEL_KERNEL_CODE << 32) |
          ((uint64_t)ZEROOS_STAR_USER_BASE << 48));
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);

    /* Mask IF, DF, AC: the kernel entry trampoline runs with interrupts
     * disabled until the task context is fully handled. */
    wrmsr(MSR_SFMASK, RFLAGS_IF | RFLAGS_DF | RFLAGS_AC);
}

/*
 * Whole-range user-pointer copy. Validates the complete range (canonical
 * addresses, user range, mapped pages, read permission) before touching
 * any byte, and walks the range page by page so the final byte of a
 * multi-page buffer is checked too.
 */
int copy_from_user(void *dest, const struct vmm_space *space,
                   uint64_t virtual_address, uint64_t length) {
    uint8_t *d = (uint8_t *)dest;

    if (length == 0)
        return 0;
    if (!vmm_space_is_user_range(space, virtual_address, length, 0))
        return -1;

    uint64_t offset = 0;
    while (offset < length) {
        uint64_t page_virtual = (virtual_address + offset) & ~(VMM_PAGE_SIZE - 1);
        uint64_t physical = vmm_space_translate(space, page_virtual);
        if (physical == 0)
            return -1;
        uint64_t page_offset = (virtual_address + offset) & (VMM_PAGE_SIZE - 1);
        uint64_t chunk = VMM_PAGE_SIZE - page_offset;
        if (chunk > length - offset)
            chunk = length - offset;
        for (uint64_t i = 0; i < chunk; ++i)
            d[offset + i] = ((uint8_t *)physical)[page_offset + i];
        offset += chunk;
    }
    return 0;
}

static void syscall_security_violation(const char *message) {
    serial_write_public("ZEROOS PANIC: ");
    serial_write_public(message);
    serial_write_public("\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

uint64_t syscall_dispatch(struct syscall_frame *frame) {
    struct task *task = task_current();
    struct process *process = task ? task->process : 0;

    /*
     * The trampoline has already proven ring-3 origin (user RSP in PML4
     * slot 254) and a valid current task. These checks are defense in
     * depth for the dispatch path itself.
     */
    if (!task || !process || !process->space.root)
        syscall_security_violation("SYSCALL without a user process");

    switch (frame->rax) {
    case ZEROOS_SYSCALL_EXIT:
        /* Never returns. */
        process_exit(frame->rdi & 0xffULL);
        frame->rax = -1;
        break;

    case ZEROOS_SYSCALL_YIELD:
        task_yield();
        frame->rax = 0;
        break;

    case ZEROOS_SYSCALL_WRITE: {
        uint64_t fd = frame->rdi;
        uint64_t buffer = frame->rsi;
        uint64_t length = frame->rdx;

        if (fd != 1) {
            frame->rax = -1;
            break;
        }
        if (length == 0) {
            frame->rax = 0;
            break;
        }
        /*
         * Reject oversized lengths outright. Capping here would turn an
         * overflowed or malformed length into a silently different (and
         * apparently successful) transfer.
         */
        if (length > ZEROOS_SYSCALL_MAX_IO) {
            frame->rax = -1;
            break;
        }

        char text[ZEROOS_SYSCALL_MAX_IO];
        if (copy_from_user(text, &process->space, buffer, length) != 0) {
            frame->rax = -1;
            break;
        }
        serial_write_public(text);
        frame->rax = length;
        break;
    }

    case ZEROOS_SYSCALL_GETPID:
        frame->rax = process->pid;
        break;

    case ZEROOS_SYSCALL_GETTID:
        frame->rax = task->id;
        break;

    default:
        frame->rax = -1;
        break;
    }

    return frame->rax;
}

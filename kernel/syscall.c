#include "syscall.h"
#include "interrupts.h"
#include "process.h"
#include "thread.h"
#include "task.h"
#include "vmm.h"
#include "cpu.h"

extern void serial_write_public(const char *text);

static uint64_t syscall_error(uint64_t error) {
    return 0ULL-error;
}

static struct process *current_process(void) {
    struct thread *thread=thread_current();
    return thread ? thread->process : (struct process *)0;
}

/*
 * User pointers are validated against the owning process before any byte is
 * touched. The physical allocator currently exposes the managed low-memory
 * window through the kernel's identity mapping, so the copy loop translates
 * every page rather than treating a user virtual address as a kernel pointer.
 */
static int copy_from_user(void *destination, uint64_t source, uint64_t length) {
    struct process *process=current_process();
    uint8_t *out=(uint8_t *)destination;
    uint64_t offset=0;

    if (!destination || !process ||
        (length && !process_address_space_is_user_range(process,source,length,0)))
        return -1;
    while (offset<length) {
        uint64_t address=source+offset;
        uint64_t physical=vmm_space_translate(&process->address_space,address);
        uint64_t within=VMM_PAGE_SIZE-(address & (VMM_PAGE_SIZE-1ULL));
        uint64_t count=length-offset<within ? length-offset : within;
        if (!physical)
            return -1;
        for (uint64_t i=0; i<count; ++i)
            out[offset+i]=((const uint8_t *)(uint64_t)(physical+i))[0];
        offset+=count;
    }
    return 0;
}

static int copy_to_user(uint64_t destination, const void *source,
                        uint64_t length) {
    struct process *process=current_process();
    const uint8_t *in=(const uint8_t *)source;
    uint64_t offset=0;

    if (!source || !process ||
        (length && !process_address_space_is_user_range(process,destination,
                                                        length,1)))
        return -1;
    while (offset<length) {
        uint64_t address=destination+offset;
        uint64_t physical=vmm_space_translate(&process->address_space,address);
        uint64_t within=VMM_PAGE_SIZE-(address & (VMM_PAGE_SIZE-1ULL));
        uint64_t count=length-offset<within ? length-offset : within;
        if (!physical)
            return -1;
        for (uint64_t i=0; i<count; ++i)
            ((uint8_t *)(uint64_t)(physical+i))[0]=in[offset+i];
        offset+=count;
    }
    return 0;
}

static void syscall_write(struct interrupt_frame *frame) {
    uint64_t fd=frame->rdi;
    uint64_t user_buffer=frame->rsi;
    uint64_t length=frame->rdx;
    char buffer[ZEROOS_SYSCALL_MAX_TRANSFER+1U];

    if (fd!=1 && fd!=2) {
        frame->rax=syscall_error(ZEROOS_EBADF);
        return;
    }
    if (length>ZEROOS_SYSCALL_MAX_TRANSFER) {
        frame->rax=syscall_error(ZEROOS_EOVERFLOW);
        return;
    }
    if (length==0) {
        frame->rax=0;
        return;
    }
    if (copy_from_user(buffer,user_buffer,length)!=0) {
        frame->rax=syscall_error(ZEROOS_EFAULT);
        return;
    }
    buffer[length]='\0';
    serial_write_public(buffer);
    frame->rax=length;
}

void syscall_dispatch(struct interrupt_frame *frame) {
    struct thread *thread;
    struct process *process;

    if (!frame || (frame->cs & 3ULL)!=3ULL) {
        if (frame)
            frame->rax=syscall_error(ZEROOS_EPERM);
        return;
    }

    thread=thread_current();
    process=current_process();
    if (!thread || !process) {
        frame->rax=syscall_error(ZEROOS_EPERM);
        return;
    }

    switch (frame->rax) {
    case ZEROOS_SYS_ABI_INFO: {
        struct zeroos_syscall_abi_info info={
            .version=ZEROOS_SYSCALL_ABI_VERSION,
            .size=(uint32_t)sizeof(info),
            .features=ZEROOS_ABI_FEATURE_PROCESS |
                      ZEROOS_ABI_FEATURE_MEMORY |
                      ZEROOS_ABI_FEATURE_INIT,
            .max_transfer=ZEROOS_SYSCALL_MAX_TRANSFER
        };
        if (frame->rdi==0 || frame->rsi<sizeof(info) ||
            copy_to_user(frame->rdi,&info,sizeof(info))!=0)
            frame->rax=syscall_error(ZEROOS_EFAULT);
        else
            frame->rax=0;
        break;
    }
    case ZEROOS_SYS_EXIT:
        /* The terminating thread will not return through this ISR. Retire
         * the per-CPU interrupt nesting before handing its task to the
         * scheduler, matching the user-exception containment path. */
        cpu_irq_exit();
        if (thread_exit(frame->rdi)!=0) {
            frame->rax=syscall_error(ZEROOS_EBUSY);
            break;
        }
        /* task_exit() has handed off the CPU. There is no safe return frame
         * for this thread, so remain unreachable if a broken scheduler ever
         * returns here. */
        for (;;) __asm__ volatile ("cli; hlt");
    case ZEROOS_SYS_WRITE:
        syscall_write(frame);
        break;
    case ZEROOS_SYS_GETPID:
        frame->rax=process->pid;
        break;
    case ZEROOS_SYS_GETTID:
        frame->rax=thread->tid;
        break;
    case ZEROOS_SYS_YIELD:
        /* The syscall is already on the interrupt frame. Publish a normal
         * timer-style reschedule request; never perform a cooperative switch
         * while the ISR stack owns the live user frame. */
        if (task_current())
            task_current()->need_resched=1;
        frame->rax=0;
        break;
    default:
        frame->rax=syscall_error(ZEROOS_ENOSYS);
        break;
    }
}

int syscall_debug_validate(void) {
    struct zeroos_syscall_abi_info info={0};
    if (ZEROOS_SYSCALL_VECTOR<32 || ZEROOS_SYSCALL_VECTOR>=256 ||
        ZEROOS_SYSCALL_ABI_VERSION==0 || sizeof(info)!=24U ||
        ZEROOS_SYSCALL_MAX_TRANSFER==0)
        return -1;
    return 0;
}

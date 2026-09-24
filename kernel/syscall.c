#include "syscall.h"
#include "interrupts.h"
#include "process.h"
#include "thread.h"
#include "task.h"
#include "timer.h"
#include "vmm.h"
#include "exec.h"
#include "cpu.h"
#include "ipc.h"
#include "shmem.h"

extern void serial_write_public(const char *text);

static uint64_t syscall_error(uint64_t error) {
    return 0ULL-error;
}

static uint64_t syscall_result(int result) {
    return result<0 ? syscall_error((uint64_t)(-result)) : (uint64_t)result;
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

static int syscall_reap_child(struct process *parent,
                               process_id_t pid,
                               uint64_t *status_out) {
    struct process *child=process_find_child(parent,pid);
    uint64_t status=0;

    if (!child || child->state!=PROCESS_ZOMBIE)
        return child ? -ZEROOS_EAGAIN : -ZEROOS_ECHILD;
    if (child->first_child || child->creating_threads ||
        child->live_thread_count!=0)
        return -ZEROOS_EBUSY;
    while (child->first_thread) {
        struct thread *thread=child->first_thread;
        if (thread->state!=THREAD_ZOMBIE ||
            thread_reap(thread,&status)!=0)
            return -ZEROOS_EBUSY;
    }
    if (vmm_activate_kernel()!=0 || process_reap(child,&status)!=0 ||
        vmm_space_activate(&parent->address_space)!=0)
        return -ZEROOS_EBUSY;
    if (status_out)
        *status_out=status;
    return 0;
}


static void syscall_create_channel(struct interrupt_frame *frame,
                                   struct process *process,
                                   uint8_t kind) {
    struct zeroos_ipc_pair pair={0};
    int result;

    if (frame->rdi==0 || frame->rsi<sizeof(pair) ||
        !process_address_space_is_user_range(process,frame->rdi,
                                             sizeof(pair),1)) {
        frame->rax=syscall_error(ZEROOS_EFAULT);
        return;
    }
    if (kind==ZEROOS_IPC_KIND_EVENT)
        result=ipc_create_event(process,&pair.local,&pair.peer);
    else if (kind==ZEROOS_IPC_KIND_PIPE)
        result=ipc_create_pipe(process,&pair.local,&pair.peer);
    else
        result=ipc_create(process,&pair.local,&pair.peer);
    if (result!=0) {
        frame->rax=syscall_result(result);
        return;
    }
    if (copy_to_user(frame->rdi,&pair,sizeof(pair))!=0) {
        (void)ipc_close(process,pair.local);
        (void)ipc_close(process,pair.peer);
        frame->rax=syscall_error(ZEROOS_EFAULT);
        return;
    }
    frame->rax=0;
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
                      ZEROOS_ABI_FEATURE_IPC |
                      ZEROOS_ABI_FEATURE_INIT |
                      ZEROOS_ABI_FEATURE_PIPE |
                      ZEROOS_ABI_FEATURE_EVENT |
                      ZEROOS_ABI_FEATURE_SHMEM,
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
    case ZEROOS_SYS_IPC_CREATE:
        syscall_create_channel(frame,process,ZEROOS_IPC_KIND_MESSAGE);
        break;
    case ZEROOS_SYS_PIPE_CREATE:
        syscall_create_channel(frame,process,ZEROOS_IPC_KIND_PIPE);
        break;
    case ZEROOS_SYS_EVENT_CREATE:
        syscall_create_channel(frame,process,ZEROOS_IPC_KIND_EVENT);
        break;
    case ZEROOS_SYS_IPC_GRANT: {
        zeroos_ipc_handle_t target_handle=0;
        struct process *target=0;
        uint8_t rights;
        int result;
        if (frame->rdx==0 ||
            !process_address_space_is_user_range(process,frame->rdx,
                                                 sizeof(target_handle),1)) {
            frame->rax=syscall_error(ZEROOS_EFAULT);
            break;
        }
        if (frame->r10>ZEROOS_IPC_ALL_RIGHTS) {
            frame->rax=syscall_error(ZEROOS_EINVAL);
            break;
        }
        rights=frame->r10 ? (uint8_t)frame->r10 : ZEROOS_IPC_ALL_RIGHTS;
        if (process_acquire_live(frame->rsi,&target)!=0)
            result=-ZEROOS_ENOENT;
        else {
            result=ipc_grant_rights(process,frame->rdi,frame->rsi,
                                    rights,&target_handle);
            if (result==0 && copy_to_user(frame->rdx,&target_handle,
                                          sizeof(target_handle))!=0) {
                /* The target owns the newly created capability. Roll it back
                 * before returning EFAULT so a faulting output pointer cannot
                 * consume a capability-table slot or extend endpoint
                 * lifetime. The outer process pin keeps close/reuse stable. */
                (void)ipc_close(target,target_handle);
                result=-ZEROOS_EFAULT;
            }
            (void)process_release_live(target);
        }
        frame->rax=syscall_result(result);
        break;
    }
    case ZEROOS_SYS_IPC_CLOSE:
        frame->rax=syscall_result(ipc_close(process,frame->rdi));
        break;
    case ZEROOS_SYS_IPC_SEND: {
        uint64_t length=frame->rdx;
        uint8_t message[ZEROOS_IPC_MAX_MESSAGE];
        if (length==0) {
            frame->rax=syscall_error(ZEROOS_EINVAL);
            break;
        }
        if (length>sizeof(message)) {
            frame->rax=syscall_error(ZEROOS_EOVERFLOW);
            break;
        }
        if (copy_from_user(message,frame->rsi,length)!=0) {
            frame->rax=syscall_error(ZEROOS_EFAULT);
            break;
        }
        frame->rax=syscall_result(ipc_send_timeout(
            process,frame->rdi,message,length,frame->r10,
            frame->r9 ? frame->r9 : ZEROOS_IPC_TIMEOUT_FOREVER));
        break;
    }
    case ZEROOS_SYS_PIPE_WRITE: {
        uint64_t length=frame->rdx;
        uint8_t bytes[ZEROOS_SYSCALL_MAX_TRANSFER];
        if (length==0) {
            frame->rax=syscall_error(ZEROOS_EINVAL);
            break;
        }
        if (length>sizeof(bytes)) {
            frame->rax=syscall_error(ZEROOS_EOVERFLOW);
            break;
        }
        if (copy_from_user(bytes,frame->rsi,length)!=0) {
            frame->rax=syscall_error(ZEROOS_EFAULT);
            break;
        }
        frame->rax=syscall_result(ipc_pipe_write_timeout(
            process,frame->rdi,bytes,length,frame->r10,
            frame->r9 ? frame->r9 : ZEROOS_IPC_TIMEOUT_FOREVER));
        break;
    }
    case ZEROOS_SYS_IPC_RECEIVE: {
        uint8_t message[ZEROOS_IPC_MAX_MESSAGE];
        uint64_t length=0;
        int result;
        if (frame->rsi==0 || frame->r8==0) {
            frame->rax=syscall_error(ZEROOS_EFAULT);
            break;
        }
        if (frame->rdx==0) {
            frame->rax=syscall_error(ZEROOS_EINVAL);
            break;
        }
        if (frame->rdx>sizeof(message)) {
            frame->rax=syscall_error(ZEROOS_EOVERFLOW);
            break;
        }
        if (!process_address_space_is_user_range(process,frame->rsi,
                                                 frame->rdx,1) ||
            !process_address_space_is_user_range(process,frame->r8,
                                                 sizeof(length),1)) {
            frame->rax=syscall_error(ZEROOS_EFAULT);
            break;
        }
        result=ipc_receive_timeout(
            process,frame->rdi,message,frame->rdx,frame->r10,&length,
            frame->r9 ? frame->r9 : ZEROOS_IPC_TIMEOUT_FOREVER);
        if (result>=0) {
            if (copy_to_user(frame->rsi,message,length)!=0 ||
                copy_to_user(frame->r8,&length,sizeof(length))!=0)
                result=-ZEROOS_EFAULT;
        }
        frame->rax=syscall_result(result);
        break;
    }
    case ZEROOS_SYS_PIPE_READ: {
        uint8_t bytes[ZEROOS_SYSCALL_MAX_TRANSFER];
        uint64_t length=0;
        int result;
        if (frame->rsi==0 || frame->r8==0) {
            frame->rax=syscall_error(ZEROOS_EFAULT);
            break;
        }
        if (frame->rdx==0) {
            frame->rax=syscall_error(ZEROOS_EINVAL);
            break;
        }
        if (frame->rdx>sizeof(bytes)) {
            frame->rax=syscall_error(ZEROOS_EOVERFLOW);
            break;
        }
        if (!process_address_space_is_user_range(process,frame->rsi,
                                                 frame->rdx,1) ||
            !process_address_space_is_user_range(process,frame->r8,
                                                 sizeof(length),1)) {
            frame->rax=syscall_error(ZEROOS_EFAULT);
            break;
        }
        result=ipc_pipe_read_timeout(
            process,frame->rdi,bytes,frame->rdx,frame->r10,&length,
            frame->r9 ? frame->r9 : ZEROOS_IPC_TIMEOUT_FOREVER);
        if (result>=0) {
            if (copy_to_user(frame->rsi,bytes,length)!=0 ||
                copy_to_user(frame->r8,&length,sizeof(length))!=0)
                result=-ZEROOS_EFAULT;
        }
        frame->rax=syscall_result(result);
        break;
    }
    case ZEROOS_SYS_EVENT_SIGNAL:
        frame->rax=syscall_result(ipc_event_signal(
            process,frame->rdi,frame->r10));
        break;
    case ZEROOS_SYS_EVENT_WAIT:
        frame->rax=syscall_result(ipc_event_wait_timeout(
            process,frame->rdi,frame->r10,
            frame->r9 ? frame->r9 : ZEROOS_IPC_TIMEOUT_FOREVER));
        break;
    case ZEROOS_SYS_EVENT_CLOSE:
        frame->rax=syscall_result(ipc_close(process,frame->rdi));
        break;
    case ZEROOS_SYS_SHM_CREATE: {
        zeroos_shmem_handle_t handle=0;
        int result;
        if (frame->rdx==0 ||
            !process_address_space_is_user_range(process,frame->rdx,
                                                 sizeof(handle),1)) {
            frame->rax=syscall_error(ZEROOS_EFAULT);
            break;
        }
        result=shmem_create(process,frame->rdi,frame->rsi,&handle);
        if (result==0 && copy_to_user(frame->rdx,&handle,sizeof(handle))!=0) {
            (void)shmem_close(process,handle);
            result=-ZEROOS_EFAULT;
        }
        frame->rax=syscall_result(result);
        break;
    }
    case ZEROOS_SYS_SHM_GRANT: {
        zeroos_shmem_handle_t target_handle=0;
        struct process *target=0;
        int result;
        if (frame->rdx==0 ||
            !process_address_space_is_user_range(process,frame->rdx,
                                                 sizeof(target_handle),1)) {
            frame->rax=syscall_error(ZEROOS_EFAULT);
            break;
        }
        if (frame->r10==0 || frame->r10>ZEROOS_SHMEM_ALL_RIGHTS) {
            frame->rax=syscall_error(ZEROOS_EINVAL);
            break;
        }
        if (process_acquire_live(frame->rsi,&target)!=0)
            result=-ZEROOS_ENOENT;
        else {
            result=shmem_grant(process,frame->rdi,frame->rsi,
                               (uint8_t)frame->r10,&target_handle);
            if (result==0 && copy_to_user(frame->rdx,&target_handle,
                                          sizeof(target_handle))!=0) {
                (void)shmem_close(target,target_handle);
                result=-ZEROOS_EFAULT;
            }
            (void)process_release_live(target);
        }
        frame->rax=syscall_result(result);
        break;
    }
    case ZEROOS_SYS_SHM_MAP: {
        uint64_t mapped=0;
        int result=shmem_map(process,frame->rdi,frame->rsi,frame->rdx,
                             &mapped);
        frame->rax=result==0 ? mapped : syscall_result(result);
        break;
    }
    case ZEROOS_SYS_SHM_UNMAP:
        frame->rax=syscall_result(shmem_unmap(process,frame->rdi,frame->rsi));
        break;
    case ZEROOS_SYS_SHM_CLOSE:
        frame->rax=syscall_result(shmem_close(process,frame->rdi));
        break;
    case ZEROOS_SYS_SPAWN: {
        struct zeroos_exec_spawn_result spawn={0};
        int result=exec_spawn(process,frame->rdi,frame->rsi,frame->rdx,
                              frame->r10,frame->r8,frame->r9,&spawn);
        frame->rax=result==0 ? spawn.pid : syscall_result(result);
        break;
    }
    case ZEROOS_SYS_WAIT: {
        uint64_t status_address=frame->rsi;
        uint64_t timeout=frame->r10;
        uint64_t deadline=0;
        int result=-ZEROOS_EINTR;

        if ((frame->rdx&~ZEROOS_WAIT_VALID_FLAGS)!=0 ||
            (status_address &&
             !process_address_space_is_user_range(process,status_address,
                                                  sizeof(uint64_t),1))) {
            frame->rax=syscall_error(ZEROOS_EFAULT);
            break;
        }
        if (timeout) {
            deadline=timer_ticks()+timeout;
            if (deadline<timer_ticks())
                deadline=~0ULL;
        }
        for (;;) {
            struct process *child=process_find_child(process,frame->rdi);
            if (!child && (frame->rdi!=0 || process_child_count(process)==0)) {
                result=-ZEROOS_ECHILD;
                break;
            }
            if (child && child->state==PROCESS_ZOMBIE) {
                process_id_t child_pid=child->pid;
                uint64_t status=0;
                result=syscall_reap_child(process,child_pid,&status);
                if (result==0 && status_address &&
                    copy_to_user(status_address,&status,sizeof(status))!=0)
                    result=-ZEROOS_EFAULT;
                if (result==0)
                    frame->rax=child_pid;
                else
                    frame->rax=syscall_result(result);
                break;
            }
            if (frame->rdx&ZEROOS_WAIT_FLAG_NONBLOCK) {
                result=-ZEROOS_EAGAIN;
                break;
            }
            if (!timeout) {
                uint64_t wait_flags=0;
                int wait_result=process_child_wait_prepare(process,frame->rdi,
                                                           &wait_flags);
                if (wait_result<0) {
                    result=wait_result;
                    break;
                }
                if (wait_result>0)
                    continue;
                if (wait_queue_commit(wait_flags)!=0) {
                    result=-ZEROOS_EINTR;
                    break;
                }
                continue;
            }
            if ((long long)(deadline-timer_ticks())<=0) {
                result=-ZEROOS_ETIMEDOUT;
                break;
            }
            {
                uint64_t wait_flags=0;
                int wait_result=process_child_wait_prepare(process,frame->rdi,
                                                           &wait_flags);
                if (wait_result<0) {
                    result=wait_result;
                    break;
                }
                if (wait_result>0)
                    continue;
                result=wait_queue_commit_until(wait_flags,deadline);
                (void)wait_queue_remove_current(&process->child_waiters);
                if (result<0) {
                    result=-ZEROOS_EINTR;
                    break;
                }
                continue;
            }
        }
        if (result!=0)
            frame->rax=syscall_result(result);
        break;
    }
    default:
        frame->rax=syscall_error(ZEROOS_ENOSYS);
        break;
    }
}

int syscall_debug_validate(void) {
    struct zeroos_syscall_abi_info info={0};
    if (ZEROOS_SYSCALL_VECTOR<32 || ZEROOS_SYSCALL_VECTOR>=256 ||
        ZEROOS_SYSCALL_ABI_VERSION==0 || sizeof(info)!=24U ||
        ZEROOS_SYSCALL_MAX_TRANSFER==0 ||
        ZEROOS_SYS_SHM_CLOSE+1U!=ZEROOS_SYS_MAX)
        return -1;
    return 0;
}

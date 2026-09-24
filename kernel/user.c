#include "user.h"
#include "memory.h"
#include "process.h"
#include "thread.h"
#include "vmm.h"
#include "syscall.h"

extern void serial_write_public(const char *text);
extern void zeroos_user_enter(uint64_t entry, uint64_t stack);

static struct process *init_process;
static struct thread *init_thread;
static uint8_t userspace_initialized;
static uint8_t init_started;
static uint8_t init_reaped;

static const char init_message[]=
    "ZEROOS: userspace init syscall path passed.\n";

static void put_u32(uint8_t *buffer, uint32_t value) {
    buffer[0]=(uint8_t)value;
    buffer[1]=(uint8_t)(value>>8);
    buffer[2]=(uint8_t)(value>>16);
    buffer[3]=(uint8_t)(value>>24);
}

static void put_u64(uint8_t *buffer, uint64_t value) {
    for (uint32_t i=0; i<8; ++i)
        buffer[i]=(uint8_t)(value>>(i*8U));
}

/* A deliberately tiny statically linked init image. Its only dependencies
 * are the versioned ABI's write and exit calls; all memory is mapped through
 * the process-owned VM quota before the thread is published. */
static uint64_t build_init_image(void *page) {
    uint8_t *code=(uint8_t *)page;
    uint64_t offset=0;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_WRITE); offset+=4;
    code[offset++]=0xbf; put_u32(&code[offset],1); offset+=4;
    code[offset++]=0x48; code[offset++]=0xbe;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE); offset+=8;
    code[offset++]=0xba;
    put_u32(&code[offset],(uint32_t)(sizeof(init_message)-1U)); offset+=4;
    code[offset++]=0xcd; code[offset++]=0x80;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_EXIT); offset+=4;
    code[offset++]=0xbf; put_u32(&code[offset],0); offset+=4;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0xf4;
    return offset;
}

static void userspace_release_page(struct process *process,
                                   uint64_t virtual_address,
                                   void *physical) {
    if (process_address_space_unmap_page(process,virtual_address)==0)
        page_free(physical);
}

int userspace_system_init(void) {
    if (userspace_initialized)
        return 0;
    if (syscall_debug_validate()!=0)
        return -1;
    init_process=0;
    init_thread=0;
    init_started=0;
    init_reaped=0;
    userspace_initialized=1;
    return 0;
}

int userspace_start_init(void) {
    void *code_page=0;
    void *data_page=0;
    void *stack_page=0;
    process_id_t pid=0;
    thread_id_t tid=0;

    if (!userspace_initialized || init_started)
        return init_started ? 0 : -1;
    if (process_create(0,&pid)!=0)
        return -1;
    init_process=process_lookup(pid);
    if (!init_process ||
        process_set_limits(init_process,4,4,16)!=0)
        return -1;

    code_page=page_alloc_zero();
    data_page=page_alloc_zero();
    stack_page=page_alloc_zero();
    if (!code_page || !data_page || !stack_page)
        goto fail;

    for (uint64_t i=0; i<sizeof(init_message)-1U; ++i)
        ((uint8_t *)data_page)[i]=(uint8_t)init_message[i];
    (void)build_init_image(code_page);

    if (process_address_space_map_page(init_process, ZEROOS_USER_CODE_BASE,
                                       (uint64_t)code_page,VMM_USER)!=0)
        goto fail;
    page_free(code_page);
    code_page=0;
    if (process_address_space_map_page(init_process, ZEROOS_USER_DATA_BASE,
                                       (uint64_t)data_page,
                                       VMM_USER|VMM_WRITABLE|VMM_NO_EXECUTE)!=0)
        goto fail;
    page_free(data_page);
    data_page=0;
    if (process_address_space_map_page(init_process, ZEROOS_USER_STACK_PAGE,
                                       (uint64_t)stack_page,
                                       VMM_USER|VMM_WRITABLE|VMM_NO_EXECUTE)!=0)
        goto fail;
    page_free(stack_page);
    stack_page=0;

    if (thread_create_user(init_process,ZEROOS_USER_CODE_BASE,
                           ZEROOS_USER_STACK_TOP,&tid)!=0)
        goto fail;
    init_thread=thread_lookup(tid);
    if (!init_thread)
        goto fail;

    init_started=1;
    serial_write_public("ZEROOS: userspace init process published.\n");
    return 0;

fail:
    if (code_page) page_free(code_page);
    if (data_page) page_free(data_page);
    if (stack_page) page_free(stack_page);
    if (init_process) {
        /* Mapped pages are released only through the ownership wrapper. */
        userspace_release_page(init_process,ZEROOS_USER_STACK_PAGE,stack_page);
        userspace_release_page(init_process,ZEROOS_USER_DATA_BASE,data_page);
        userspace_release_page(init_process,ZEROOS_USER_CODE_BASE,code_page);
    }
    return -1;
}

int user_thread_enter(struct thread *thread) {
    struct process *process;
    if (!thread || !thread_is_user(thread) ||
        !(process=thread->process) ||
        !vmm_space_is_executable(&process->address_space,
                                 thread->user_entry,1) ||
        !process_address_space_is_user_range(process,thread->user_stack-1ULL,1,1))
        return -1;
    if (vmm_space_activate(&process->address_space)!=0)
        return -1;
    zeroos_user_enter(thread->user_entry,thread->user_stack);
    return -1;
}

int userspace_service_step(void) {
    uint64_t status=0;
    if (!init_started || init_reaped)
        return 0;
    if (!init_thread || init_thread->state!=THREAD_ZOMBIE)
        return 0;
    if (thread_reap(init_thread,&status)!=0)
        return -1;
    if (!init_process || init_process->state!=PROCESS_ZOMBIE)
        return -1;
    if (vmm_activate_kernel()!=0 || process_reap(init_process,&status)!=0)
        return -1;
    init_reaped=1;
    serial_write_public("ZEROOS: init userspace process reaped cleanly.\n");
    return 0;
}

int userspace_debug_validate(void) {
    if (!userspace_initialized)
        return -1;
    if (init_reaped && (!init_process || !init_thread))
        return -1;
    return init_reaped ? 1 : 0;
}

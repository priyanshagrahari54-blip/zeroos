#include "user.h"
#include "elf.h"
#include "memory.h"
#include "process.h"
#include "thread.h"
#include "vmm.h"
#include "syscall.h"
#include "ipc.h"

extern void serial_write_public(const char *text);
extern void zeroos_user_enter(uint64_t entry, uint64_t stack);

static struct process *init_process;
static struct thread *init_thread;
static struct process *service_process;
static struct thread *service_thread;
static struct process *service_controller;
static zeroos_ipc_handle_t service_controller_handle;
static zeroos_ipc_handle_t service_controller_peer;
static uint64_t service_attempt;
static uint64_t service_expected_length;
static uint8_t service_started;
static uint8_t service_recovered;
static uint8_t userspace_initialized;
static uint8_t init_started;
static uint8_t init_reaped;

static const char init_message[]=
    "ZEROOS: userspace init syscall path passed.\n";

#define INIT_ELF_DATA_OFFSET 0x1000ULL
#define INIT_ELF_ENTRY_OFFSET 0x100ULL
#define INIT_ELF_IMAGE_SIZE (INIT_ELF_DATA_OFFSET+sizeof(init_message)-1U)
static uint8_t init_elf_image[INIT_ELF_IMAGE_SIZE];

#define SERVICE_ELF_DATA_OFFSET 0x1000ULL
#define SERVICE_ELF_ENTRY_OFFSET 0x100ULL
#define SERVICE_ELF_IMAGE_SIZE (SERVICE_ELF_DATA_OFFSET+128U)
static uint8_t service_elf_image[SERVICE_ELF_IMAGE_SIZE];

static const char service_message_one[]=
    "ZEROOS: isolated service IPC attempt one.\n";
static const char service_message_two[]=
    "ZEROOS: isolated service IPC attempt two.\n";

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

/* A deliberately tiny statically linked init image. It is represented as a
 * real ET_EXEC ELF object so every boot exercises the same loader checks that
 * later service binaries will use. */
static uint64_t build_init_code(uint8_t *code) {
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

static uint64_t build_init_elf(void) {
    struct zeroos_elf64_ehdr *header=
        (struct zeroos_elf64_ehdr *)(uint64_t)init_elf_image;
    struct zeroos_elf64_phdr *code_segment=
        (struct zeroos_elf64_phdr *)(uint64_t)(init_elf_image+sizeof(*header));
    struct zeroos_elf64_phdr *data_segment=code_segment+1;
    for (uint64_t i=0; i<sizeof(init_elf_image); ++i)
        init_elf_image[i]=0;

    header->ident[0]=0x7f;
    header->ident[1]='E';
    header->ident[2]='L';
    header->ident[3]='F';
    header->ident[4]=2;
    header->ident[5]=1;
    header->ident[6]=1;
    header->type=ZEROOS_ELF_ET_EXEC;
    header->machine=ZEROOS_ELF_EM_X86_64;
    header->version=1;
    header->entry=ZEROOS_USER_CODE_BASE+INIT_ELF_ENTRY_OFFSET;
    header->phoff=sizeof(*header);
    header->ehsize=sizeof(*header);
    header->phentsize=sizeof(*code_segment);
    header->phnum=2;

    code_segment->type=ZEROOS_ELF_PT_LOAD;
    code_segment->flags=ZEROOS_ELF_PF_R|ZEROOS_ELF_PF_X;
    code_segment->offset=0;
    code_segment->virtual_address=ZEROOS_USER_CODE_BASE;
    code_segment->file_size=VMM_PAGE_SIZE;
    code_segment->memory_size=VMM_PAGE_SIZE;
    code_segment->alignment=VMM_PAGE_SIZE;

    data_segment->type=ZEROOS_ELF_PT_LOAD;
    data_segment->flags=ZEROOS_ELF_PF_R|ZEROOS_ELF_PF_W;
    data_segment->offset=INIT_ELF_DATA_OFFSET;
    data_segment->virtual_address=ZEROOS_USER_DATA_BASE;
    data_segment->file_size=sizeof(init_message)-1U;
    data_segment->memory_size=VMM_PAGE_SIZE;
    data_segment->alignment=VMM_PAGE_SIZE;

    (void)build_init_code(init_elf_image+INIT_ELF_ENTRY_OFFSET);
    for (uint64_t i=0; i<sizeof(init_message)-1U; ++i)
        init_elf_image[INIT_ELF_DATA_OFFSET+i]=(uint8_t)init_message[i];
    return INIT_ELF_IMAGE_SIZE;
}

static uint64_t build_service_code(uint8_t *code,
                                   zeroos_ipc_handle_t handle,
                                   uint64_t message_length,
                                   uint64_t exit_status) {
    uint64_t offset=0;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_IPC_SEND); offset+=4;
    code[offset++]=0x48; code[offset++]=0xbf;
    put_u64(&code[offset],handle); offset+=8;
    code[offset++]=0x48; code[offset++]=0xbe;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE); offset+=8;
    code[offset++]=0xba;
    put_u32(&code[offset],(uint32_t)message_length); offset+=4;
    code[offset++]=0x41; code[offset++]=0xba;
    put_u32(&code[offset],ZEROOS_IPC_FLAG_NONBLOCK); offset+=4;
    code[offset++]=0x49; code[offset++]=0xb9;
    put_u64(&code[offset],ZEROOS_IPC_TIMEOUT_FOREVER); offset+=8;
    code[offset++]=0xcd; code[offset++]=0x80;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_EXIT); offset+=4;
    code[offset++]=0xbf; put_u32(&code[offset],(uint32_t)exit_status); offset+=4;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0xf4;
    return offset;
}

static uint64_t build_service_elf(zeroos_ipc_handle_t handle,
                                  const char *message,
                                  uint64_t message_length,
                                  uint64_t exit_status) {
    struct zeroos_elf64_ehdr *header=
        (struct zeroos_elf64_ehdr *)(uint64_t)service_elf_image;
    struct zeroos_elf64_phdr *code_segment=
        (struct zeroos_elf64_phdr *)(uint64_t)(service_elf_image+sizeof(*header));
    struct zeroos_elf64_phdr *data_segment=code_segment+1;

    for (uint64_t i=0; i<sizeof(service_elf_image); ++i)
        service_elf_image[i]=0;
    if (message_length>sizeof(service_elf_image)-SERVICE_ELF_DATA_OFFSET)
        return 0;

    header->ident[0]=0x7f;
    header->ident[1]='E';
    header->ident[2]='L';
    header->ident[3]='F';
    header->ident[4]=2;
    header->ident[5]=1;
    header->ident[6]=1;
    header->type=ZEROOS_ELF_ET_EXEC;
    header->machine=ZEROOS_ELF_EM_X86_64;
    header->version=1;
    header->entry=ZEROOS_USER_CODE_BASE+SERVICE_ELF_ENTRY_OFFSET;
    header->phoff=sizeof(*header);
    header->ehsize=sizeof(*header);
    header->phentsize=sizeof(*code_segment);
    header->phnum=2;

    code_segment->type=ZEROOS_ELF_PT_LOAD;
    code_segment->flags=ZEROOS_ELF_PF_R|ZEROOS_ELF_PF_X;
    code_segment->offset=0;
    code_segment->virtual_address=ZEROOS_USER_CODE_BASE;
    code_segment->file_size=VMM_PAGE_SIZE;
    code_segment->memory_size=VMM_PAGE_SIZE;
    code_segment->alignment=VMM_PAGE_SIZE;

    data_segment->type=ZEROOS_ELF_PT_LOAD;
    data_segment->flags=ZEROOS_ELF_PF_R|ZEROOS_ELF_PF_W;
    data_segment->offset=SERVICE_ELF_DATA_OFFSET;
    data_segment->virtual_address=ZEROOS_USER_DATA_BASE;
    data_segment->file_size=message_length;
    data_segment->memory_size=VMM_PAGE_SIZE;
    data_segment->alignment=VMM_PAGE_SIZE;

    (void)build_service_code(service_elf_image+SERVICE_ELF_ENTRY_OFFSET,
                             handle,message_length,exit_status);
    for (uint64_t i=0; i<message_length; ++i)
        service_elf_image[SERVICE_ELF_DATA_OFFSET+i]=(uint8_t)message[i];
    return SERVICE_ELF_DATA_OFFSET+message_length;
}

static int userspace_ipc_self_test(struct process *process) {
    zeroos_ipc_handle_t local=0;
    zeroos_ipc_handle_t peer=0;
    uint8_t send_buffer[ZEROOS_IPC_MAX_MESSAGE];
    uint8_t receive_buffer[ZEROOS_IPC_MAX_MESSAGE];
    uint64_t length=0;

    for (uint32_t i=0; i<sizeof(send_buffer); ++i)
        send_buffer[i]=(uint8_t)('A'+(i%26U));
    if (ipc_create(process,&local,&peer)!=0)
        return -1;
    for (uint32_t i=0; i<ZEROOS_IPC_QUEUE_DEPTH; ++i)
        if (ipc_send(process,local,send_buffer,16,
                     ZEROOS_IPC_FLAG_NONBLOCK)!=16)
            goto fail;
    if (ipc_send(process,local,send_buffer,16,
                 ZEROOS_IPC_FLAG_NONBLOCK)!=-ZEROOS_EAGAIN ||
        ipc_send_timeout(process,local,send_buffer,16,0,0)!=-ZEROOS_ETIMEDOUT ||
        ipc_send(process,local,send_buffer,16,1ULL<<7)!=-ZEROOS_EINVAL ||
        ipc_send(process,local,send_buffer,0,0)!=-ZEROOS_EINVAL ||
        ipc_send(process,local+0x100ULL,send_buffer,16,
                 ZEROOS_IPC_FLAG_NONBLOCK)!=-ZEROOS_EBADF ||
        ipc_receive(process,peer,receive_buffer,1,
                    ZEROOS_IPC_FLAG_NONBLOCK,&length)!=-ZEROOS_EOVERFLOW ||
        ipc_receive(process,peer,receive_buffer,sizeof(receive_buffer),
                    ZEROOS_IPC_FLAG_PEEK,&length)!=16 || length!=16 ||
        ipc_receive(process,peer,receive_buffer,sizeof(receive_buffer),
                    ZEROOS_IPC_FLAG_NONBLOCK,&length)!=16 || length!=16)
        goto fail;
    while (ipc_receive(process,peer,receive_buffer,sizeof(receive_buffer),
                       ZEROOS_IPC_FLAG_NONBLOCK,&length)==16) {}
    if (ipc_receive_timeout(process,peer,receive_buffer,sizeof(receive_buffer),
                            0,&length,0)!=-ZEROOS_ETIMEDOUT ||
        ipc_close(process,local)!=0 ||
        ipc_receive(process,peer,receive_buffer,sizeof(receive_buffer),
                    ZEROOS_IPC_FLAG_NONBLOCK,&length)!=-ZEROOS_EPIPE ||
        ipc_close(process,peer)!=0 || ipc_debug_validate()!=0)
        return -1;
    return 0;

fail:
    (void)ipc_close(process,local);
    (void)ipc_close(process,peer);
    return -1;
}

static void userspace_release_page(struct process *process,
                                   uint64_t virtual_address,
                                   void *physical) {
    if (process_address_space_unmap_page(process,virtual_address)==0 &&
        physical)
        page_free(physical);
}

static int userspace_start_service(uint64_t attempt) {
    const char *message;
    uint64_t message_length;
    uint64_t image_size;
    uint64_t worker_pid=0;
    uint64_t controller_pid=0;
    thread_id_t thread_id=0;
    struct process *worker=0;
    struct process *controller=0;
    struct thread *thread=0;
    struct zeroos_elf_load_result load_result={0};
    void *stack_page=0;
    uint8_t image_loaded=0;
    uint8_t stack_mapped=0;
    zeroos_ipc_handle_t controller_handle=0;
    zeroos_ipc_handle_t controller_peer=0;
    zeroos_ipc_handle_t worker_handle=0;

    if (attempt!=1 && attempt!=2)
        return -1;
    if (attempt==1) {
        message=service_message_one;
        message_length=sizeof(service_message_one)-1U;
    } else {
        message=service_message_two;
        message_length=sizeof(service_message_two)-1U;
    }
    image_size=build_service_elf(0,message,message_length,
                                 attempt==1 ? 7U : 0U);
    if (!image_size)
        return -1;

    if (process_create(0,&controller_pid)!=0)
        return -1;
    controller=process_lookup(controller_pid);
    if (!controller || process_set_limits(controller,1,1,4)!=0)
        goto fail;
    if (process_create(0,&worker_pid)!=0)
        goto fail;
    worker=process_lookup(worker_pid);
    if (!worker || process_set_limits(worker,1,1,16)!=0)
        goto fail;

    /* The supervisor keeps one endpoint pair in a kernel-owned controller
     * process.  Only the worker receives the granted capability, so delivery
     * below exercises the same cross-process capability path as production
     * services rather than a same-owner shortcut. */
    if (ipc_create(controller,&controller_handle,&controller_peer)!=0 ||
        ipc_grant(controller,controller_peer,worker->pid,&worker_handle)!=0)
        goto fail;
    image_size=build_service_elf(worker_handle,message,message_length,
                                 attempt==1 ? 7U : 0U);
    if (!image_size ||
        elf_load_image(worker,service_elf_image,image_size,&load_result)!=0 ||
        load_result.entry!=ZEROOS_USER_CODE_BASE+SERVICE_ELF_ENTRY_OFFSET)
        goto fail;
    image_loaded=1;

    stack_page=page_alloc_zero();
    if (!stack_page ||
        process_address_space_map_page(worker,ZEROOS_USER_STACK_PAGE,
                                       (uint64_t)stack_page,
                                       VMM_USER|VMM_WRITABLE|VMM_NO_EXECUTE)!=0)
        goto fail;
    page_free(stack_page);
    stack_page=0;
    stack_mapped=1;

    if (thread_create_user(worker,load_result.entry,
                           ZEROOS_USER_STACK_TOP,&thread_id)!=0)
        goto fail;
    thread=thread_lookup(thread_id);
    if (!thread) {
        serial_write_public("ZEROOS PANIC: service thread lookup failed.\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }

    service_controller=controller;
    service_process=worker;
    service_thread=thread;
    service_controller_handle=controller_handle;
    service_controller_peer=controller_peer;
    service_attempt=attempt;
    service_expected_length=message_length;
    service_started=1;
    if (attempt==1)
        serial_write_public("ZEROOS: service manager launched isolated IPC service attempt 1.\n");
    else
        serial_write_public("ZEROOS: service manager launched isolated IPC service attempt 2.\n");
    return 0;

fail:
    if (stack_page)
        page_free(stack_page);
    if (stack_mapped && worker)
        userspace_release_page(worker,ZEROOS_USER_STACK_PAGE,0);
    if (image_loaded && worker)
        (void)elf_unload_image(worker,service_elf_image,image_size);
    if (worker && worker->state==PROCESS_NEW)
        (void)process_abort_new(worker);
    if (controller) {
        (void)ipc_close(controller,controller_handle);
        (void)ipc_close(controller,controller_peer);
        if (controller->state==PROCESS_NEW)
            (void)process_abort_new(controller);
    }
    return -1;
}

static int userspace_finish_service(void) {
    uint8_t receive_buffer[ZEROOS_IPC_MAX_MESSAGE];
    uint64_t received_length=0;
    uint64_t status=0;
    int receive_result;

    if (!service_started || !service_controller || !service_process ||
        !service_thread || service_thread->state!=THREAD_ZOMBIE)
        return 0;
    receive_result=ipc_receive(service_controller,service_controller_handle,
                               receive_buffer,sizeof(receive_buffer),
                               ZEROOS_IPC_FLAG_NONBLOCK,&received_length);
    if (receive_result<0 || (uint64_t)receive_result!=service_expected_length ||
        received_length!=service_expected_length)
        return -1;
    for (uint64_t i=0; i<service_expected_length; ++i) {
        uint8_t expected;
        if (service_attempt==1)
            expected=(uint8_t)service_message_one[i];
        else
            expected=(uint8_t)service_message_two[i];
        if (receive_buffer[i]!=expected)
            return -1;
    }
    if (thread_reap(service_thread,&status)!=0 ||
        service_process->state!=PROCESS_ZOMBIE ||
        vmm_activate_kernel()!=0 || process_reap(service_process,&status)!=0)
        return -1;

    (void)ipc_close(service_controller,service_controller_handle);
    (void)ipc_close(service_controller,service_controller_peer);
    if (service_controller->state!=PROCESS_NEW ||
        process_abort_new(service_controller)!=0 || ipc_debug_validate()!=0)
        return -1;

    service_process=0;
    service_thread=0;
    service_controller=0;
    service_controller_handle=0;
    service_controller_peer=0;
    service_started=0;
    if (service_attempt==1) {
        if (status!=7)
            return -1;
        serial_write_public("ZEROOS: service manager restarted failed service after IPC delivery.\n");
        if (userspace_start_service(2)!=0)
            return -1;
        return 0;
    }
    if (status!=0)
        return -1;
    service_recovered=1;
    serial_write_public("ZEROOS: isolated service IPC/restart recovery passed.\n");
    return 0;
}

int userspace_system_init(void) {
    if (userspace_initialized)
        return 0;
    if (syscall_debug_validate()!=0 || ipc_debug_validate()!=0 ||
        elf_debug_validate()!=0 || elf_system_init()!=0)
        return -1;
    init_process=0;
    init_thread=0;
    service_process=0;
    service_thread=0;
    service_controller=0;
    service_controller_handle=0;
    service_controller_peer=0;
    service_attempt=0;
    service_expected_length=0;
    service_started=0;
    service_recovered=0;
    init_started=0;
    init_reaped=0;
    userspace_initialized=1;
    return 0;
}

int userspace_start_init(void) {
    void *stack_page=0;
    process_id_t pid=0;
    thread_id_t tid=0;
    struct zeroos_elf_load_result load_result={0};
    uint64_t image_size=build_init_elf();
    uint8_t image_loaded=0;
    uint8_t stack_mapped=0;

    if (!userspace_initialized || init_started)
        return init_started ? 0 : -1;
    if (process_create(0,&pid)!=0)
        return -1;
    init_process=process_lookup(pid);
    if (!init_process || process_set_limits(init_process,4,4,16)!=0)
        goto fail;
    if (userspace_ipc_self_test(init_process)!=0) {
        serial_write_public("ZEROOS PANIC: capability IPC queue self-test failed.\n");
        goto fail;
    }
    serial_write_public("ZEROOS: capability IPC queue/backpressure self-test passed.\n");
    serial_write_public("ZEROOS: capability IPC negative/timeout semantics passed.\n");

    if (elf_load_image(init_process,init_elf_image,image_size,
                       &load_result)!=0 ||
        load_result.entry!=ZEROOS_USER_CODE_BASE+INIT_ELF_ENTRY_OFFSET)
        goto fail;
    image_loaded=1;

    stack_page=page_alloc_zero();
    if (!stack_page ||
        process_address_space_map_page(init_process,ZEROOS_USER_STACK_PAGE,
                                       (uint64_t)stack_page,
                                       VMM_USER|VMM_WRITABLE|VMM_NO_EXECUTE)!=0)
        goto fail;
    page_free(stack_page);
    stack_page=0;
    stack_mapped=1;

    if (thread_create_user(init_process,load_result.entry,
                           ZEROOS_USER_STACK_TOP,&tid)!=0)
        goto fail;
    init_thread=thread_lookup(tid);
    if (!init_thread) {
        serial_write_public("ZEROOS PANIC: published user thread lookup failed.\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }

    init_started=1;
    if (userspace_start_service(1)!=0)
        return -1;
    serial_write_public("ZEROOS: userspace init process published.\n");
    return 0;

fail:
    if (stack_page)
        page_free(stack_page);
    if (stack_mapped && init_process)
        userspace_release_page(init_process,ZEROOS_USER_STACK_PAGE,0);
    if (image_loaded && init_process)
        (void)elf_unload_image(init_process,init_elf_image,image_size);
    if (init_process) {
        if (process_abort_new(init_process)==0)
            init_process=0;
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
    if (service_started) {
        if (userspace_finish_service()!=0)
            return -1;
        if (service_started || !service_recovered)
            return 0;
    }
    if (!init_thread || init_thread->state!=THREAD_ZOMBIE)
        return 0;
    if (thread_reap(init_thread,&status)!=0)
        return -1;
    if (!init_process || init_process->state!=PROCESS_ZOMBIE)
        return -1;
    if (vmm_activate_kernel()!=0 || process_reap(init_process,&status)!=0 ||
        ipc_debug_validate()!=0)
        return -1;
    init_reaped=1;
    serial_write_public("ZEROOS: init userspace process reaped cleanly.\n");
    return 0;
}

int userspace_debug_validate(void) {
    if (!userspace_initialized)
        return -1;
    if (init_reaped && (!init_process || !init_thread ||
                        service_started || !service_recovered))
        return -1;
    return init_reaped ? 1 : 0;
}

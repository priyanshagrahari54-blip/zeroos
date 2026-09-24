#include "user.h"
#include "elf.h"
#include "exec.h"
#include "memory.h"
#include "process.h"
#include "thread.h"
#include "task.h"
#include "scheduler.h"
#include "sync.h"
#include "vmm.h"
#include "syscall.h"
#include "ipc.h"
#include "shmem.h"

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
static uint64_t service_request_length;
static uint8_t service_request_sent;
static uint8_t service_started;
static uint8_t service_recovered;
static uint8_t userspace_initialized;
static uint8_t init_started;
static uint8_t init_reaped;
static struct atomic_u64 event_probe_state;
static struct process *event_probe_process;
static zeroos_ipc_handle_t event_probe_signal;
static zeroos_ipc_handle_t event_probe_wait;
static struct atomic_u64 ipc_close_probe_state;
static struct process *ipc_close_probe_process;
static zeroos_ipc_handle_t ipc_close_probe_signal;
static zeroos_ipc_handle_t ipc_close_probe_wait;
static struct atomic_u64 ipc_send_probe_state;
static struct process *ipc_send_probe_process;
static zeroos_ipc_handle_t ipc_send_probe_signal;
static zeroos_ipc_handle_t ipc_send_probe_wait;

static const char init_message[]=
    "ZEROOS: userspace init syscall path passed.\n";

#define INIT_ELF_DATA_OFFSET 0x1000ULL
#define INIT_ELF_ENTRY_OFFSET 0x100ULL
#define INIT_ELF_CHILD_FILE_OFFSET 0x2000ULL
#define INIT_ELF_STATUS_OFFSET 0x4000ULL
#define INIT_ELF_EVENT_OFFSET 0x3000ULL
#define INIT_ELF_PIPE_BUFFER_OFFSET 0x3200ULL
#define INIT_ELF_PIPE_LENGTH_OFFSET 0x3300ULL
#define INIT_ELF_ABI_OFFSET 0x3400ULL
#define INIT_ELF_ARGV_OFFSET 0x3600ULL
#define INIT_ELF_ENVP_OFFSET 0x3620ULL
#define INIT_ELF_ARG0_OFFSET 0x3640ULL
#define INIT_ELF_ARG1_OFFSET 0x3660ULL
#define INIT_ELF_ENV0_OFFSET 0x3680ULL
#define INIT_ELF_DATA_FILE_END (INIT_ELF_STATUS_OFFSET+sizeof(uint64_t))
#define INIT_ELF_DATA_MEMORY_SIZE ((INIT_ELF_DATA_FILE_END-INIT_ELF_DATA_OFFSET+\
                                    VMM_PAGE_SIZE-1ULL)&~(VMM_PAGE_SIZE-1ULL))
#define INIT_ELF_IMAGE_SIZE INIT_ELF_DATA_FILE_END
static uint8_t init_elf_image[INIT_ELF_IMAGE_SIZE];

static const char init_child_message[]=
    "ZEROOS: spawned child argv/envp runtime path passed.\n";
static const char init_child_argument_zero[]="zeroos-child";
static const char init_child_argument_one[]="hello";
static const char init_child_environment_zero[]="ZEROOS_MODE=production";

#define INIT_CHILD_ELF_DATA_OFFSET 0x1000ULL
#define INIT_CHILD_ELF_ENTRY_OFFSET 0x100ULL
#define INIT_CHILD_ELF_IMAGE_SIZE \
    (INIT_CHILD_ELF_DATA_OFFSET+sizeof(init_child_message)-1U)
static uint8_t init_child_elf_image[INIT_CHILD_ELF_IMAGE_SIZE];

#define SERVICE_ELF_DATA_OFFSET 0x1000ULL
#define SERVICE_ELF_ENTRY_OFFSET 0x100ULL
#define SERVICE_ELF_IMAGE_SIZE (SERVICE_ELF_DATA_OFFSET+256U)
static uint8_t service_elf_image[SERVICE_ELF_IMAGE_SIZE];

static const char service_message_one[]=
    "ZEROOS: isolated service IPC attempt one.\n";
static const char service_message_two[]=
    "ZEROOS: isolated service IPC attempt two.\n";
static const char service_request_one[]=
    "ZEROOS: service manager request one.\n";
static const char service_request_two[]=
    "ZEROOS: service manager request two.\n";

#define SERVICE_ACK_OFFSET 0x80ULL
#define SERVICE_RECEIVE_LENGTH_OFFSET 0x100ULL

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

static uint64_t build_child_code(uint8_t *code) {
    uint64_t offset=0;
    uint64_t failure_jumps[8];
    uint32_t failure_jump_count=0;
    uint64_t failure_label;

    /* Validate the initial process stack before producing the success marker.
     * This turns argv/envp construction into an executing contract rather than
     * merely a loader-side memory-layout claim. */
    code[offset++]=0x48; code[offset++]=0x83; code[offset++]=0x3c;
    code[offset++]=0x24; code[offset++]=2;
    failure_jumps[failure_jump_count++]=offset;
    code[offset++]=0x75; code[offset++]=0;
    code[offset++]=0x48; code[offset++]=0x83; code[offset++]=0x7c;
    code[offset++]=0x24; code[offset++]=0x18; code[offset++]=0;
    failure_jumps[failure_jump_count++]=offset;
    code[offset++]=0x75; code[offset++]=0;
    code[offset++]=0x48; code[offset++]=0x83; code[offset++]=0x7c;
    code[offset++]=0x24; code[offset++]=0x28; code[offset++]=0;
    failure_jumps[failure_jump_count++]=offset;
    code[offset++]=0x75; code[offset++]=0;

    code[offset++]=0x48; code[offset++]=0x8b; code[offset++]=0x5c;
    code[offset++]=0x24; code[offset++]=8;
    code[offset++]=0x80; code[offset++]=0x3b; code[offset++]=(uint8_t)'z';
    failure_jumps[failure_jump_count++]=offset;
    code[offset++]=0x75; code[offset++]=0;
    code[offset++]=0x48; code[offset++]=0x8b; code[offset++]=0x5c;
    code[offset++]=0x24; code[offset++]=16;
    code[offset++]=0x80; code[offset++]=0x3b; code[offset++]=(uint8_t)'h';
    failure_jumps[failure_jump_count++]=offset;
    code[offset++]=0x75; code[offset++]=0;
    code[offset++]=0x48; code[offset++]=0x8b; code[offset++]=0x5c;
    code[offset++]=0x24; code[offset++]=32;
    code[offset++]=0x80; code[offset++]=0x3b; code[offset++]=(uint8_t)'Z';
    failure_jumps[failure_jump_count++]=offset;
    code[offset++]=0x75; code[offset++]=0;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_WRITE); offset+=4;
    code[offset++]=0xbf; put_u32(&code[offset],1); offset+=4;
    code[offset++]=0x48; code[offset++]=0xbe;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE); offset+=8;
    code[offset++]=0xba;
    put_u32(&code[offset],(uint32_t)(sizeof(init_child_message)-1U)); offset+=4;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_EXIT); offset+=4;
    code[offset++]=0xbf; put_u32(&code[offset],0); offset+=4;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0xf4;

    failure_label=offset;
    for (uint32_t i=0; i<failure_jump_count; ++i)
        code[failure_jumps[i]+1]=(uint8_t)(failure_label-(failure_jumps[i]+2ULL));
    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_EXIT); offset+=4;
    code[offset++]=0xbf; put_u32(&code[offset],10); offset+=4;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0xf4;
    return offset;
}

static uint64_t build_child_elf(void) {
    struct zeroos_elf64_ehdr *header=
        (struct zeroos_elf64_ehdr *)(uint64_t)init_child_elf_image;
    struct zeroos_elf64_phdr *code_segment=
        (struct zeroos_elf64_phdr *)(uint64_t)(init_child_elf_image+sizeof(*header));
    struct zeroos_elf64_phdr *data_segment=code_segment+1;
    for (uint64_t i=0; i<sizeof(init_child_elf_image); ++i)
        init_child_elf_image[i]=0;

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
    header->entry=ZEROOS_USER_CODE_BASE+INIT_CHILD_ELF_ENTRY_OFFSET;
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
    data_segment->offset=INIT_CHILD_ELF_DATA_OFFSET;
    data_segment->virtual_address=ZEROOS_USER_DATA_BASE;
    data_segment->file_size=sizeof(init_child_message)-1U;
    data_segment->memory_size=VMM_PAGE_SIZE;
    data_segment->alignment=VMM_PAGE_SIZE;

    (void)build_child_code(init_child_elf_image+INIT_CHILD_ELF_ENTRY_OFFSET);
    for (uint64_t i=0; i<sizeof(init_child_message)-1U; ++i)
        init_child_elf_image[INIT_CHILD_ELF_DATA_OFFSET+i]=
            (uint8_t)init_child_message[i];
    return INIT_CHILD_ELF_IMAGE_SIZE;
}

/* A deliberately tiny statically linked init image. It is represented as a
 * real ET_EXEC ELF object so every boot exercises the same loader checks that
 * later service binaries will use. */
static uint64_t build_init_code(uint8_t *code) {
    uint64_t offset=0;
    uint64_t failure_jumps[20];
    uint32_t failure_jump_count=0;
    uint64_t failure_label;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_WRITE); offset+=4;
    code[offset++]=0xbf; put_u32(&code[offset],1); offset+=4;
    code[offset++]=0x48; code[offset++]=0xbe;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE); offset+=8;
    code[offset++]=0xba;
    put_u32(&code[offset],(uint32_t)(sizeof(init_message)-1U)); offset+=4;
    code[offset++]=0xcd; code[offset++]=0x80;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_ABI_INFO); offset+=4;
    code[offset++]=0x48; code[offset++]=0xbf;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE+
            (INIT_ELF_ABI_OFFSET-INIT_ELF_DATA_OFFSET)); offset+=8;
    code[offset++]=0xbe; put_u32(&code[offset],24); offset+=4;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0x48; code[offset++]=0x85; code[offset++]=0xc0;
    failure_jumps[failure_jump_count++]=offset;
    code[offset++]=0x0f; code[offset++]=0x88;
    put_u32(&code[offset],0); offset+=4;

    /* Negative ABI probes run from Ring 3 and must fail closed without
     * creating a capability, child, or address-space side effect. */
    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_IPC_CREATE); offset+=4;
    code[offset++]=0x48; code[offset++]=0x31; code[offset++]=0xff;
    code[offset++]=0xbe; put_u32(&code[offset],16); offset+=4;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0x48; code[offset++]=0x85; code[offset++]=0xc0;
    failure_jumps[failure_jump_count++]=offset;
    code[offset++]=0x0f; code[offset++]=0x89;
    put_u32(&code[offset],0); offset+=4;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_WAIT); offset+=4;
    code[offset++]=0x48; code[offset++]=0x31; code[offset++]=0xff;
    code[offset++]=0x48; code[offset++]=0xbe;
    put_u64(&code[offset],0x100ULL); offset+=8;
    code[offset++]=0x48; code[offset++]=0x31; code[offset++]=0xd2;
    code[offset++]=0x49; code[offset++]=0x31; code[offset++]=0xd2;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0x48; code[offset++]=0x85; code[offset++]=0xc0;
    failure_jumps[failure_jump_count++]=offset;
    code[offset++]=0x0f; code[offset++]=0x89;
    put_u32(&code[offset],0); offset+=4;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_SPAWN); offset+=4;
    code[offset++]=0x48; code[offset++]=0xbf;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE); offset+=8;
    code[offset++]=0xbe;
    put_u32(&code[offset],(uint32_t)(sizeof(init_message)-1U)); offset+=4;
    code[offset++]=0x48; code[offset++]=0x31; code[offset++]=0xd2;
    code[offset++]=0x41; code[offset++]=0xba; put_u32(&code[offset],0); offset+=4;
    code[offset++]=0x4d; code[offset++]=0x31; code[offset++]=0xc0;
    code[offset++]=0x4d; code[offset++]=0x31; code[offset++]=0xc9;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0x48; code[offset++]=0x85; code[offset++]=0xc0;
    failure_jumps[failure_jump_count++]=offset;
    code[offset++]=0x0f; code[offset++]=0x89;
    put_u32(&code[offset],0); offset+=4;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_SPAWN); offset+=4;
    code[offset++]=0x48; code[offset++]=0xbf;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE+0x8000ULL); offset+=8;
    code[offset++]=0xbe; put_u32(&code[offset],0x100); offset+=4;
    code[offset++]=0x48; code[offset++]=0x31; code[offset++]=0xd2;
    code[offset++]=0x41; code[offset++]=0xba; put_u32(&code[offset],0); offset+=4;
    code[offset++]=0x4d; code[offset++]=0x31; code[offset++]=0xc0;
    code[offset++]=0x4d; code[offset++]=0x31; code[offset++]=0xc9;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0x48; code[offset++]=0x85; code[offset++]=0xc0;
    failure_jumps[failure_jump_count++]=offset;
    code[offset++]=0x0f; code[offset++]=0x89;
    put_u32(&code[offset],0); offset+=4;

    /* Exercise the dedicated event and bounded record-pipe syscall names
     * before the child-spawn path. Both are built on the same capability and
     * wait/wakeup invariants, but their ABI contracts are independently gated. */
    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_EVENT_CREATE); offset+=4;
    code[offset++]=0x48; code[offset++]=0xbf;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE+
            (INIT_ELF_EVENT_OFFSET-INIT_ELF_DATA_OFFSET)); offset+=8;
    code[offset++]=0xbe; put_u32(&code[offset],16); offset+=4;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0x48; code[offset++]=0x85; code[offset++]=0xc0;
    failure_jumps[failure_jump_count++]=offset;
    code[offset++]=0x0f; code[offset++]=0x88;
    put_u32(&code[offset],0); offset+=4;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_EVENT_SIGNAL); offset+=4;
    code[offset++]=0x48; code[offset++]=0xbf;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE+
            (INIT_ELF_EVENT_OFFSET-INIT_ELF_DATA_OFFSET)); offset+=8;
    code[offset++]=0x48; code[offset++]=0x8b; code[offset++]=0x3f;
    code[offset++]=0x49; code[offset++]=0x31; code[offset++]=0xd2;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0x48; code[offset++]=0x83; code[offset++]=0xf8; code[offset++]=1;
    failure_jumps[failure_jump_count++]=offset;
    code[offset++]=0x0f; code[offset++]=0x85;
    put_u32(&code[offset],0); offset+=4;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_EVENT_WAIT); offset+=4;
    code[offset++]=0x48; code[offset++]=0xbf;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE+
            (INIT_ELF_EVENT_OFFSET-INIT_ELF_DATA_OFFSET+8ULL)); offset+=8;
    code[offset++]=0x48; code[offset++]=0x8b; code[offset++]=0x3f;
    code[offset++]=0x41; code[offset++]=0xba;
    put_u32(&code[offset],ZEROOS_IPC_FLAG_NONBLOCK); offset+=4;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0x48; code[offset++]=0x83; code[offset++]=0xf8; code[offset++]=1;
    failure_jumps[failure_jump_count++]=offset;
    code[offset++]=0x0f; code[offset++]=0x85;
    put_u32(&code[offset],0); offset+=4;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_EVENT_CLOSE); offset+=4;
    code[offset++]=0x48; code[offset++]=0xbf;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE+
            (INIT_ELF_EVENT_OFFSET-INIT_ELF_DATA_OFFSET)); offset+=8;
    code[offset++]=0x48; code[offset++]=0x8b; code[offset++]=0x3f;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0x48; code[offset++]=0x85; code[offset++]=0xc0;
    failure_jumps[failure_jump_count++]=offset;
    code[offset++]=0x0f; code[offset++]=0x88;
    put_u32(&code[offset],0); offset+=4;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_EVENT_CLOSE); offset+=4;
    code[offset++]=0x48; code[offset++]=0xbf;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE+
            (INIT_ELF_EVENT_OFFSET-INIT_ELF_DATA_OFFSET+8ULL)); offset+=8;
    code[offset++]=0x48; code[offset++]=0x8b; code[offset++]=0x3f;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0x48; code[offset++]=0x85; code[offset++]=0xc0;
    failure_jumps[failure_jump_count++]=offset;
    code[offset++]=0x0f; code[offset++]=0x88;
    put_u32(&code[offset],0); offset+=4;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_PIPE_CREATE); offset+=4;
    code[offset++]=0x48; code[offset++]=0xbf;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE+
            (INIT_ELF_EVENT_OFFSET-INIT_ELF_DATA_OFFSET+16ULL)); offset+=8;
    code[offset++]=0xbe; put_u32(&code[offset],16); offset+=4;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0x48; code[offset++]=0x85; code[offset++]=0xc0;
    failure_jumps[failure_jump_count++]=offset;
    code[offset++]=0x0f; code[offset++]=0x88;
    put_u32(&code[offset],0); offset+=4;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_PIPE_WRITE); offset+=4;
    code[offset++]=0x48; code[offset++]=0xbf;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE+
            (INIT_ELF_EVENT_OFFSET-INIT_ELF_DATA_OFFSET+16ULL)); offset+=8;
    code[offset++]=0x48; code[offset++]=0x8b; code[offset++]=0x3f;
    code[offset++]=0x48; code[offset++]=0xbe;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE); offset+=8;
    code[offset++]=0xba;
    put_u32(&code[offset],(uint32_t)(sizeof(init_message)-1U)); offset+=4;
    code[offset++]=0x41; code[offset++]=0xba;
    put_u32(&code[offset],ZEROOS_IPC_FLAG_NONBLOCK); offset+=4;
    code[offset++]=0x49; code[offset++]=0x31; code[offset++]=0xc9;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0x48; code[offset++]=0x3d;
    put_u32(&code[offset],(uint32_t)(sizeof(init_message)-1U)); offset+=4;
    failure_jumps[failure_jump_count++]=offset;
    code[offset++]=0x0f; code[offset++]=0x85;
    put_u32(&code[offset],0); offset+=4;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_PIPE_READ); offset+=4;
    code[offset++]=0x48; code[offset++]=0xbf;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE+
            (INIT_ELF_EVENT_OFFSET-INIT_ELF_DATA_OFFSET+24ULL)); offset+=8;
    code[offset++]=0x48; code[offset++]=0x8b; code[offset++]=0x3f;
    code[offset++]=0x48; code[offset++]=0xbe;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE+
            (INIT_ELF_PIPE_BUFFER_OFFSET-INIT_ELF_DATA_OFFSET)); offset+=8;
    code[offset++]=0xba; put_u32(&code[offset],ZEROOS_IPC_MAX_MESSAGE); offset+=4;
    code[offset++]=0x41; code[offset++]=0xba;
    put_u32(&code[offset],ZEROOS_IPC_FLAG_NONBLOCK); offset+=4;
    code[offset++]=0x49; code[offset++]=0xb8;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE+
            (INIT_ELF_PIPE_LENGTH_OFFSET-INIT_ELF_DATA_OFFSET)); offset+=8;
    code[offset++]=0x49; code[offset++]=0x31; code[offset++]=0xc9;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0x48; code[offset++]=0x3d;
    put_u32(&code[offset],(uint32_t)(sizeof(init_message)-1U)); offset+=4;
    failure_jumps[failure_jump_count++]=offset;
    code[offset++]=0x0f; code[offset++]=0x85;
    put_u32(&code[offset],0); offset+=4;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_IPC_CLOSE); offset+=4;
    code[offset++]=0x48; code[offset++]=0xbf;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE+
            (INIT_ELF_EVENT_OFFSET-INIT_ELF_DATA_OFFSET+16ULL)); offset+=8;
    code[offset++]=0x48; code[offset++]=0x8b; code[offset++]=0x3f;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0x48; code[offset++]=0x85; code[offset++]=0xc0;
    failure_jumps[failure_jump_count++]=offset;
    code[offset++]=0x0f; code[offset++]=0x88;
    put_u32(&code[offset],0); offset+=4;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_IPC_CLOSE); offset+=4;
    code[offset++]=0x48; code[offset++]=0xbf;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE+
            (INIT_ELF_EVENT_OFFSET-INIT_ELF_DATA_OFFSET+24ULL)); offset+=8;
    code[offset++]=0x48; code[offset++]=0x8b; code[offset++]=0x3f;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0x48; code[offset++]=0x85; code[offset++]=0xc0;
    failure_jumps[failure_jump_count++]=offset;
    code[offset++]=0x0f; code[offset++]=0x88;
    put_u32(&code[offset],0); offset+=4;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_SHM_CREATE); offset+=4;
    code[offset++]=0xbf; put_u32(&code[offset],VMM_PAGE_SIZE); offset+=4;
    code[offset++]=0x48; code[offset++]=0x31; code[offset++]=0xf6;
    code[offset++]=0x48; code[offset++]=0xba;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE+
            (INIT_ELF_EVENT_OFFSET-INIT_ELF_DATA_OFFSET+32ULL)); offset+=8;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0x48; code[offset++]=0x85; code[offset++]=0xc0;
    failure_jumps[failure_jump_count++]=offset;
    code[offset++]=0x0f; code[offset++]=0x88;
    put_u32(&code[offset],0); offset+=4;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_SHM_MAP); offset+=4;
    code[offset++]=0x48; code[offset++]=0xbf;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE+
            (INIT_ELF_EVENT_OFFSET-INIT_ELF_DATA_OFFSET+32ULL)); offset+=8;
    code[offset++]=0x48; code[offset++]=0x8b; code[offset++]=0x3f;
    code[offset++]=0x48; code[offset++]=0xbe;
    put_u64(&code[offset],ZEROOS_USER_BASE+0x10000ULL); offset+=8;
    code[offset++]=0xba; put_u32(&code[offset],ZEROOS_SHMEM_MAP_WRITE); offset+=4;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0x48; code[offset++]=0x85; code[offset++]=0xc0;
    failure_jumps[failure_jump_count++]=offset;
    code[offset++]=0x0f; code[offset++]=0x88;
    put_u32(&code[offset],0); offset+=4;
    code[offset++]=0x49; code[offset++]=0x89; code[offset++]=0xc4;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_SHM_UNMAP); offset+=4;
    code[offset++]=0x48; code[offset++]=0xbf;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE+
            (INIT_ELF_EVENT_OFFSET-INIT_ELF_DATA_OFFSET+32ULL)); offset+=8;
    code[offset++]=0x48; code[offset++]=0x8b; code[offset++]=0x3f;
    code[offset++]=0x4c; code[offset++]=0x89; code[offset++]=0xe6;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0x48; code[offset++]=0x85; code[offset++]=0xc0;
    failure_jumps[failure_jump_count++]=offset;
    code[offset++]=0x0f; code[offset++]=0x88;
    put_u32(&code[offset],0); offset+=4;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_SHM_CLOSE); offset+=4;
    code[offset++]=0x48; code[offset++]=0xbf;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE+
            (INIT_ELF_EVENT_OFFSET-INIT_ELF_DATA_OFFSET+32ULL)); offset+=8;
    code[offset++]=0x48; code[offset++]=0x8b; code[offset++]=0x3f;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0x48; code[offset++]=0x85; code[offset++]=0xc0;
    failure_jumps[failure_jump_count++]=offset;
    code[offset++]=0x0f; code[offset++]=0x88;
    put_u32(&code[offset],0); offset+=4;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_SPAWN); offset+=4;
    code[offset++]=0x48; code[offset++]=0xbf;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE+
            (INIT_ELF_CHILD_FILE_OFFSET-INIT_ELF_DATA_OFFSET)); offset+=8;
    code[offset++]=0x48; code[offset++]=0xbe;
    put_u64(&code[offset],INIT_CHILD_ELF_IMAGE_SIZE); offset+=8;
    code[offset++]=0x48; code[offset++]=0xba;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE+
            (INIT_ELF_ARGV_OFFSET-INIT_ELF_DATA_OFFSET)); offset+=8;
    code[offset++]=0x41; code[offset++]=0xba; put_u32(&code[offset],2); offset+=4;
    code[offset++]=0x49; code[offset++]=0xb8;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE+
            (INIT_ELF_ENVP_OFFSET-INIT_ELF_DATA_OFFSET)); offset+=8;
    code[offset++]=0x49; code[offset++]=0xc7; code[offset++]=0xc1; put_u32(&code[offset],1); offset+=4;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0x49; code[offset++]=0x89; code[offset++]=0xc4;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_WAIT); offset+=4;
    code[offset++]=0x4c; code[offset++]=0x89; code[offset++]=0xe7;
    code[offset++]=0x48; code[offset++]=0xbe;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE+
            (INIT_ELF_STATUS_OFFSET-INIT_ELF_DATA_OFFSET)); offset+=8;
    code[offset++]=0x48; code[offset++]=0x31; code[offset++]=0xd2;
    code[offset++]=0x4d; code[offset++]=0x31; code[offset++]=0xd2;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0x48; code[offset++]=0x85; code[offset++]=0xc0;
    failure_jumps[failure_jump_count++]=offset;
    code[offset++]=0x0f; code[offset++]=0x88;
    put_u32(&code[offset],0); offset+=4;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_EXIT); offset+=4;
    code[offset++]=0xbf; put_u32(&code[offset],0); offset+=4;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0xf4;

    failure_label=offset;
    for (uint32_t i=0; i<failure_jump_count; ++i)
        put_u32(&code[failure_jumps[i]+2],
                (uint32_t)(failure_label-(failure_jumps[i]+6ULL)));
    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_EXIT); offset+=4;
    code[offset++]=0xbf; put_u32(&code[offset],9); offset+=4;
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
    uint64_t child_image_size=build_child_elf();
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
    data_segment->file_size=INIT_ELF_IMAGE_SIZE-INIT_ELF_DATA_OFFSET;
    data_segment->memory_size=INIT_ELF_DATA_MEMORY_SIZE;
    data_segment->alignment=VMM_PAGE_SIZE;

    if (build_init_code(init_elf_image+INIT_ELF_ENTRY_OFFSET)>VMM_PAGE_SIZE) {
        serial_write_public("ZEROOS PANIC: init syscall probe image exceeds one code page.\n");
        return 0;
    }
    for (uint64_t i=0; i<sizeof(init_message)-1U; ++i)
        init_elf_image[INIT_ELF_DATA_OFFSET+i]=(uint8_t)init_message[i];
    for (uint64_t i=0; i<child_image_size; ++i)
        init_elf_image[INIT_ELF_CHILD_FILE_OFFSET+i]=init_child_elf_image[i];
    put_u64(&init_elf_image[INIT_ELF_ARGV_OFFSET],
            ZEROOS_USER_DATA_BASE+
            (INIT_ELF_ARG0_OFFSET-INIT_ELF_DATA_OFFSET));
    put_u64(&init_elf_image[INIT_ELF_ARGV_OFFSET+8ULL],
            ZEROOS_USER_DATA_BASE+
            (INIT_ELF_ARG1_OFFSET-INIT_ELF_DATA_OFFSET));
    put_u64(&init_elf_image[INIT_ELF_ARGV_OFFSET+16ULL],0);
    put_u64(&init_elf_image[INIT_ELF_ENVP_OFFSET],
            ZEROOS_USER_DATA_BASE+
            (INIT_ELF_ENV0_OFFSET-INIT_ELF_DATA_OFFSET));
    put_u64(&init_elf_image[INIT_ELF_ENVP_OFFSET+8ULL],0);
    for (uint64_t i=0; i<sizeof(init_child_argument_zero); ++i)
        init_elf_image[INIT_ELF_ARG0_OFFSET+i]=init_child_argument_zero[i];
    for (uint64_t i=0; i<sizeof(init_child_argument_one); ++i)
        init_elf_image[INIT_ELF_ARG1_OFFSET+i]=init_child_argument_one[i];
    for (uint64_t i=0; i<sizeof(init_child_environment_zero); ++i)
        init_elf_image[INIT_ELF_ENV0_OFFSET+i]=init_child_environment_zero[i];
    return INIT_ELF_IMAGE_SIZE;
}

static uint64_t build_service_code(uint8_t *code,
                                   zeroos_ipc_handle_t handle,
                                   uint64_t target_pid,
                                   uint64_t message_length,
                                   uint64_t exit_status) {
    uint64_t offset=0;
    uint64_t permission_failure_jump;

    /* A transferred worker capability deliberately lacks GRANT. Attempting
     * to delegate it must fail before the service enters its blocking receive. */
    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_IPC_GRANT); offset+=4;
    code[offset++]=0x48; code[offset++]=0xbf;
    put_u64(&code[offset],handle); offset+=8;
    code[offset++]=0x48; code[offset++]=0xbe;
    put_u64(&code[offset],target_pid); offset+=8;
    code[offset++]=0x48; code[offset++]=0xba;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE+
            SERVICE_RECEIVE_LENGTH_OFFSET); offset+=8;
    code[offset++]=0x41; code[offset++]=0xba;
    put_u32(&code[offset],ZEROOS_IPC_RIGHT_SEND); offset+=4;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0x48; code[offset++]=0x83; code[offset++]=0xf8;
    code[offset++]=(uint8_t)(0U-ZEROOS_EBADF);
    permission_failure_jump=offset;
    code[offset++]=0x0f; code[offset++]=0x85;
    put_u32(&code[offset],0); offset+=4;

    /* Block until the supervisor has observed this task in the scheduler's
     * wait state. The reply remains in a separate data-page slot because the
     * receive copy is allowed to overwrite the request buffer. */
    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_IPC_RECEIVE); offset+=4;
    code[offset++]=0x48; code[offset++]=0xbf;
    put_u64(&code[offset],handle); offset+=8;
    code[offset++]=0x48; code[offset++]=0xbe;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE); offset+=8;
    code[offset++]=0xba;
    put_u32(&code[offset],ZEROOS_IPC_MAX_MESSAGE); offset+=4;
    code[offset++]=0x41; code[offset++]=0xba;
    put_u32(&code[offset],0); offset+=4;
    code[offset++]=0x49; code[offset++]=0xb8;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE+
            SERVICE_RECEIVE_LENGTH_OFFSET); offset+=8;
    code[offset++]=0x49; code[offset++]=0xb9;
    put_u64(&code[offset],ZEROOS_IPC_TIMEOUT_FOREVER); offset+=8;
    code[offset++]=0xcd; code[offset++]=0x80;

    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_IPC_SEND); offset+=4;
    code[offset++]=0x48; code[offset++]=0xbf;
    put_u64(&code[offset],handle); offset+=8;
    code[offset++]=0x48; code[offset++]=0xbe;
    put_u64(&code[offset],ZEROOS_USER_DATA_BASE+SERVICE_ACK_OFFSET); offset+=8;
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

    uint64_t permission_failure_label=offset;
    put_u32(&code[permission_failure_jump+2],
            (uint32_t)(permission_failure_label-(permission_failure_jump+6ULL)));
    code[offset++]=0xb8; put_u32(&code[offset],ZEROOS_SYS_EXIT); offset+=4;
    code[offset++]=0xbf; put_u32(&code[offset],9); offset+=4;
    code[offset++]=0xcd; code[offset++]=0x80;
    code[offset++]=0xf4;
    return offset;
}

static uint64_t build_service_elf(zeroos_ipc_handle_t handle,
                                  uint64_t target_pid,
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
    if (message_length>sizeof(service_elf_image)-SERVICE_ELF_DATA_OFFSET-
                         SERVICE_ACK_OFFSET)
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
    data_segment->file_size=SERVICE_ACK_OFFSET+message_length;
    data_segment->memory_size=VMM_PAGE_SIZE;
    data_segment->alignment=VMM_PAGE_SIZE;

    (void)build_service_code(service_elf_image+SERVICE_ELF_ENTRY_OFFSET,
                             handle,target_pid,message_length,exit_status);
    for (uint64_t i=0; i<message_length; ++i)
        service_elf_image[SERVICE_ELF_DATA_OFFSET+SERVICE_ACK_OFFSET+i]=
            (uint8_t)message[i];
    return SERVICE_ELF_DATA_OFFSET+SERVICE_ACK_OFFSET+message_length;
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
        ipc_send_timeout(process,local,send_buffer,16,0,2)!=-ZEROOS_ETIMEDOUT ||
        ipc_send(process,local,send_buffer,1ULL<<9,0)!=-ZEROOS_EINVAL ||
        ipc_send(process,local,send_buffer,16,1ULL<<7)!=-ZEROOS_EINVAL ||
        ipc_send(process,local,send_buffer,0,0)!=-ZEROOS_EINVAL ||
        ipc_grant_rights(process,local,process->pid,0,0)!=-ZEROOS_EINVAL ||
        ipc_grant_rights(process,local,process->pid,1U<<4,0)!=-ZEROOS_EINVAL ||
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
        ipc_receive_timeout(process,peer,receive_buffer,sizeof(receive_buffer),
                            0,&length,2)!=-ZEROOS_ETIMEDOUT ||
        ipc_close(process,local)!=0 ||
        ipc_receive(process,peer,receive_buffer,sizeof(receive_buffer),
                    ZEROOS_IPC_FLAG_NONBLOCK,&length)!=-ZEROOS_EPIPE ||
        ipc_close(process,peer)!=0 || ipc_debug_validate()!=0)
        return -1;

    {
        zeroos_ipc_handle_t signal_handle=0;
        zeroos_ipc_handle_t wait_handle=0;
        if (ipc_create_event(process,&signal_handle,&wait_handle)!=0 ||
            ipc_send(process,signal_handle,send_buffer,1,
                     ZEROOS_IPC_FLAG_NONBLOCK)!=-ZEROOS_EINVAL ||
            ipc_receive(process,wait_handle,receive_buffer,sizeof(receive_buffer),
                        ZEROOS_IPC_FLAG_NONBLOCK,&length)!=-ZEROOS_EINVAL ||
            ipc_event_signal(process,signal_handle,0)!=1 ||
            ipc_event_signal(process,signal_handle,0)!=0 ||
            ipc_event_signal(process,signal_handle,1ULL<<1)!=-ZEROOS_EINVAL ||
            ipc_event_wait_timeout(process,wait_handle,
                                   ZEROOS_IPC_FLAG_PEEK,0)!=1 ||
            ipc_event_wait_timeout(process,wait_handle,
                                   ZEROOS_IPC_FLAG_NONBLOCK,0)!=1 ||
            ipc_event_wait_timeout(process,wait_handle,
                                   ZEROOS_IPC_FLAG_NONBLOCK,0)!=-ZEROOS_EAGAIN ||
            ipc_event_wait_timeout(process,wait_handle,1ULL<<2,0)!=-ZEROOS_EINVAL ||
            ipc_event_wait_timeout(process,wait_handle,0,0)!=-ZEROOS_ETIMEDOUT ||
            ipc_event_wait_timeout(process,wait_handle,0,2)!=-ZEROOS_ETIMEDOUT ||
            ipc_close(process,signal_handle)!=0 ||
            ipc_event_wait_timeout(process,wait_handle,
                                   ZEROOS_IPC_FLAG_NONBLOCK,0)!=-ZEROOS_EPIPE ||
            ipc_close(process,wait_handle)!=0 || ipc_debug_validate()!=0) {
            (void)ipc_close(process,signal_handle);
            (void)ipc_close(process,wait_handle);
            return -1;
        }
    }
    return 0;

fail:
    (void)ipc_close(process,local);
    (void)ipc_close(process,peer);
    return -1;
}

static int userspace_shmem_self_test(struct process *process) {
    zeroos_shmem_handle_t handle=0;
    zeroos_shmem_handle_t target_handle=0;
    process_id_t target_pid=0;
    struct process *target=0;
    uint64_t first_address=ZEROOS_USER_BASE+0x10000ULL;
    uint64_t second_address=ZEROOS_USER_BASE+0x20000ULL;
    uint64_t target_address=ZEROOS_USER_BASE+0x10000ULL;
    uint64_t mapped=0;
    uint64_t physical=0;

    if (!process || shmem_create(process,0,0,&handle)!=-ZEROOS_EINVAL ||
        shmem_create(process,VMM_PAGE_SIZE*(ZEROOS_SHMEM_MAX_PAGES+1ULL),
                     0,&handle)!=-ZEROOS_EINVAL ||
        shmem_create(process,VMM_PAGE_SIZE,1,&handle)!=-ZEROOS_EINVAL ||
        shmem_create(process,VMM_PAGE_SIZE*2ULL,0,&handle)!=0)
        goto fail;
    if (shmem_map(process,handle,first_address+1ULL,0,&mapped)!=-ZEROOS_EINVAL ||
        shmem_map(process,handle,first_address,ZEROOS_SHMEM_MAP_WRITE,
                  &mapped)!=0)
        goto fail;
    physical=vmm_space_translate(&process->address_space,first_address);
    if (!physical || memory_page_references(physical)!=2 ||
        !process_address_space_is_user_range(process,first_address,
                                             VMM_PAGE_SIZE*2ULL,1) ||
        !process_address_space_is_user_range(process,first_address,
                                             VMM_PAGE_SIZE*2ULL,0))
        goto fail;
    ((uint8_t *)(uint64_t)physical)[0]=0x5a;

    if (shmem_map(process,handle,second_address,0,&mapped)!=0 ||
        vmm_space_translate(&process->address_space,second_address)!=physical ||
        process_address_space_is_user_range(process,second_address,
                                            VMM_PAGE_SIZE*2ULL,1) ||
        ((uint8_t *)(uint64_t)vmm_space_translate(
            &process->address_space,second_address))[0]!=0x5a ||
        shmem_close(process,handle)!=-ZEROOS_EBUSY ||
        shmem_map(process,handle,first_address,0,&mapped)==0 ||
        shmem_unmap(process,handle,first_address+VMM_PAGE_SIZE)!=-ZEROOS_EINVAL ||
        shmem_unmap(process,handle+0x100ULL,first_address)!=-ZEROOS_EBADF)
        goto fail;
    if (shmem_unmap(process,handle,second_address)!=0 ||
        shmem_unmap(process,handle,first_address)!=0 ||
        memory_page_references(physical)!=1)
        goto fail;

    if (process_create(process,&target_pid)!=0)
        goto fail;
    target=process_lookup(target_pid);
    if (!target || shmem_grant(process,handle,target_pid,
                               ZEROOS_SHMEM_RIGHT_MAP|ZEROOS_SHMEM_RIGHT_CLOSE,
                               &target_handle)!=0 ||
        shmem_map(target,target_handle,target_address,0,&mapped)!=0 ||
        shmem_map(target,target_handle,target_address+0x10000ULL,
                  ZEROOS_SHMEM_MAP_WRITE,&mapped)!=-ZEROOS_EPERM ||
        shmem_unmap(target,target_handle,target_address)!=0 ||
        shmem_close(target,target_handle)!=0 ||
        process_abort_new(target)!=0)
        goto fail;
    target=0;
    target_handle=0;

    if (shmem_close(process,handle)!=0 || shmem_debug_validate()!=0)
        goto fail;
    handle=0;
    return 0;

fail:
    if (target)
        (void)process_abort_new(target);
    if (target_handle && target)
        (void)shmem_close(target,target_handle);
    if (handle)
        (void)shmem_close(process,handle);
    (void)shmem_debug_validate();
    return -1;
}

static void event_probe_entry(void *argument) {
    struct process *process=(struct process *)argument;
    atomic_u64_store(&event_probe_state,1);
    int result=ipc_event_wait_timeout(process,event_probe_wait,0,
                                      ZEROOS_IPC_TIMEOUT_FOREVER);
    atomic_u64_store(&event_probe_state,result==1 ? 2 : 3);
}

static int userspace_event_blocking_self_test(void) {
    process_id_t pid=0;
    thread_id_t tid=0;
    struct thread *thread=0;
    uint64_t status=~0ULL;
    uint8_t thread_created=0;
    uint8_t blocked_seen=0;
    int result=-1;

    event_probe_process=0;
    event_probe_signal=0;
    event_probe_wait=0;
    atomic_u64_init(&event_probe_state,0);
    if (process_create(0,&pid)!=0)
        return -1;
    event_probe_process=process_lookup(pid);
    if (!event_probe_process ||
        process_set_limits(event_probe_process,1,1,4)!=0 ||
        ipc_create_event(event_probe_process,&event_probe_signal,
                         &event_probe_wait)!=0 ||
        thread_create_kernel(event_probe_process,event_probe_entry,
                             event_probe_process,&tid)!=0)
        goto fail;
    thread_created=1;
    thread=thread_lookup(tid);
    if (!thread)
        goto fail;

    for (uint64_t i=0; i<100000ULL; ++i) {
        uint64_t state=atomic_u64_load(&event_probe_state);
        struct task *task=thread->scheduler_task_id ?
                          task_lookup(thread->scheduler_task_id) : 0;
        if (state==1 && task && task->state==TASK_BLOCKED) {
            blocked_seen=1;
            break;
        }
        if (state>=2)
            break;
        scheduler_yield();
    }
    if (!blocked_seen || atomic_u64_load(&event_probe_state)!=1 ||
        ipc_event_signal(event_probe_process,event_probe_signal,0)!=1)
        goto release_waiter;
    for (uint64_t i=0; i<100000ULL; ++i) {
        if (atomic_u64_load(&event_probe_state)>=2)
            break;
        scheduler_yield();
    }
    if (atomic_u64_load(&event_probe_state)!=2 ||
        thread->state!=THREAD_ZOMBIE ||
        thread_reap(thread,&status)!=0 || status!=0 ||
        event_probe_process->state!=PROCESS_ZOMBIE ||
        vmm_activate_kernel()!=0 ||
        process_reap(event_probe_process,&status)!=0 || status!=0 ||
        process_lookup(pid)!=0 || ipc_debug_validate()!=0) {
        thread=0;
        goto fail;
    }
    thread=0;
    thread_created=0;
    event_probe_process=0;
    event_probe_signal=0;
    event_probe_wait=0;
    return 0;

release_waiter:
    /* A failed observation must still release a blocked waiter before the
     * temporary process can be reclaimed. */
    if (thread_created && event_probe_process && event_probe_signal) {
        (void)ipc_event_signal(event_probe_process,event_probe_signal,0);
        for (uint64_t i=0; i<100000ULL &&
             atomic_u64_load(&event_probe_state)<2; ++i)
            scheduler_yield();
    }
fail:
    if (thread_created && thread && thread->state==THREAD_ZOMBIE) {
        (void)thread_reap(thread,&status);
        thread=0;
    }
    if (event_probe_process && event_probe_process->state==PROCESS_ZOMBIE) {
        (void)vmm_activate_kernel();
        (void)process_reap(event_probe_process,&status);
    } else if (event_probe_process && event_probe_process->state==PROCESS_NEW) {
        if (event_probe_signal)
            (void)ipc_close(event_probe_process,event_probe_signal);
        if (event_probe_wait)
            (void)ipc_close(event_probe_process,event_probe_wait);
        (void)process_abort_new(event_probe_process);
    }
    event_probe_process=0;
    event_probe_signal=0;
    event_probe_wait=0;
    return result;
}

static void ipc_close_probe_entry(void *argument) {
    struct process *process=(struct process *)argument;
    uint8_t buffer[64];
    uint64_t length=0;
    int result=ipc_receive_timeout(process,ipc_close_probe_wait,
                                   buffer,sizeof(buffer),0,&length,
                                   ZEROOS_IPC_TIMEOUT_FOREVER);
    atomic_u64_store(&ipc_close_probe_state,
                     result==-ZEROOS_EPIPE ? 2 : 3);
}

static int userspace_ipc_close_wakeup_self_test(void) {
    process_id_t pid=0;
    thread_id_t tid=0;
    struct thread *thread=0;
    uint64_t status=~0ULL;
    uint8_t thread_created=0;
    uint8_t blocked_seen=0;
    int result=-1;

    ipc_close_probe_process=0;
    ipc_close_probe_signal=0;
    ipc_close_probe_wait=0;
    atomic_u64_init(&ipc_close_probe_state,0);
    if (process_create(0,&pid)!=0)
        return -1;
    ipc_close_probe_process=process_lookup(pid);
    if (!ipc_close_probe_process ||
        process_set_limits(ipc_close_probe_process,1,1,4)!=0 ||
        ipc_create(ipc_close_probe_process,&ipc_close_probe_signal,
                   &ipc_close_probe_wait)!=0 ||
        thread_create_kernel(ipc_close_probe_process,ipc_close_probe_entry,
                             ipc_close_probe_process,&tid)!=0)
        goto fail;
    thread_created=1;
    thread=thread_lookup(tid);
    if (!thread)
        goto fail;

    for (uint64_t i=0; i<100000ULL; ++i) {
        uint64_t state=atomic_u64_load(&ipc_close_probe_state);
        struct task *task=thread->scheduler_task_id ?
                          task_lookup(thread->scheduler_task_id) : 0;
        if (state==0 && task && task->state==TASK_BLOCKED) {
            blocked_seen=1;
            break;
        }
        if (state>=2)
            break;
        scheduler_yield();
    }
    if (!blocked_seen || atomic_u64_load(&ipc_close_probe_state)!=0 ||
        ipc_close(ipc_close_probe_process,ipc_close_probe_signal)!=0)
        goto release_waiter;
    ipc_close_probe_signal=0;
    for (uint64_t i=0; i<100000ULL; ++i) {
        if (atomic_u64_load(&ipc_close_probe_state)>=2)
            break;
        scheduler_yield();
    }
    if (atomic_u64_load(&ipc_close_probe_state)!=2 ||
        thread->state!=THREAD_ZOMBIE ||
        thread_reap(thread,&status)!=0 || status!=0 ||
        ipc_close_probe_process->state!=PROCESS_ZOMBIE ||
        vmm_activate_kernel()!=0 ||
        process_reap(ipc_close_probe_process,&status)!=0 || status!=0 ||
        process_lookup(pid)!=0 || ipc_debug_validate()!=0) {
        thread=0;
        goto fail;
    }
    thread=0;
    thread_created=0;
    ipc_close_probe_process=0;
    ipc_close_probe_signal=0;
    ipc_close_probe_wait=0;
    return 0;

release_waiter:
    /* Closing the sender is the wakeup under test; if setup failed, close it
     * here as well so a receiver cannot survive temporary-process teardown. */
    if (ipc_close_probe_process && ipc_close_probe_signal) {
        (void)ipc_close(ipc_close_probe_process,ipc_close_probe_signal);
        ipc_close_probe_signal=0;
    }
    for (uint64_t i=0; i<100000ULL &&
         atomic_u64_load(&ipc_close_probe_state)<2; ++i)
        scheduler_yield();
fail:
    if (thread_created && thread && thread->state==THREAD_ZOMBIE) {
        (void)thread_reap(thread,&status);
        thread=0;
    }
    if (ipc_close_probe_process &&
        ipc_close_probe_process->state==PROCESS_ZOMBIE) {
        (void)vmm_activate_kernel();
        (void)process_reap(ipc_close_probe_process,&status);
    } else if (ipc_close_probe_process &&
               ipc_close_probe_process->state==PROCESS_NEW) {
        if (ipc_close_probe_signal)
            (void)ipc_close(ipc_close_probe_process,ipc_close_probe_signal);
        if (ipc_close_probe_wait)
            (void)ipc_close(ipc_close_probe_process,ipc_close_probe_wait);
        (void)process_abort_new(ipc_close_probe_process);
    }
    ipc_close_probe_process=0;
    ipc_close_probe_signal=0;
    ipc_close_probe_wait=0;
    return result;
}

static void ipc_send_probe_entry(void *argument) {
    struct process *process=(struct process *)argument;
    uint8_t byte='S';
    int result;
    atomic_u64_store(&ipc_send_probe_state,1);
    result=ipc_send_timeout(process,ipc_send_probe_signal,&byte,1,0,
                            ZEROOS_IPC_TIMEOUT_FOREVER);
    atomic_u64_store(&ipc_send_probe_state,result==1 ? 2 : 3);
}

static int userspace_ipc_send_wakeup_self_test(void) {
    process_id_t pid=0;
    thread_id_t tid=0;
    struct thread *thread=0;
    uint8_t byte='F';
    uint64_t length=0;
    uint64_t status=~0ULL;
    uint8_t thread_created=0;
    uint8_t blocked_seen=0;
    int result=-1;

    ipc_send_probe_process=0;
    ipc_send_probe_signal=0;
    ipc_send_probe_wait=0;
    atomic_u64_init(&ipc_send_probe_state,0);
    if (process_create(0,&pid)!=0)
        return -1;
    ipc_send_probe_process=process_lookup(pid);
    if (!ipc_send_probe_process ||
        process_set_limits(ipc_send_probe_process,1,1,4)!=0 ||
        ipc_create(ipc_send_probe_process,&ipc_send_probe_signal,
                   &ipc_send_probe_wait)!=0)
        goto fail;
    for (uint32_t i=0; i<ZEROOS_IPC_QUEUE_DEPTH; ++i)
        if (ipc_send(ipc_send_probe_process,ipc_send_probe_signal,&byte,1,
                     ZEROOS_IPC_FLAG_NONBLOCK)!=1)
            goto fail;
    if (thread_create_kernel(ipc_send_probe_process,ipc_send_probe_entry,
                             ipc_send_probe_process,&tid)!=0)
        goto fail;
    thread_created=1;
    thread=thread_lookup(tid);
    if (!thread)
        goto fail;

    for (uint64_t i=0; i<100000ULL; ++i) {
        uint64_t state=atomic_u64_load(&ipc_send_probe_state);
        struct task *task=thread->scheduler_task_id ?
                          task_lookup(thread->scheduler_task_id) : 0;
        if (state==1 && task && task->state==TASK_BLOCKED) {
            blocked_seen=1;
            break;
        }
        if (state>=2)
            break;
        scheduler_yield();
    }
    if (!blocked_seen || atomic_u64_load(&ipc_send_probe_state)!=1 ||
        ipc_receive(ipc_send_probe_process,ipc_send_probe_wait,&byte,
                    sizeof(byte),ZEROOS_IPC_FLAG_NONBLOCK,&length)!=1 ||
        length!=1)
        goto release_sender;
    for (uint64_t i=0; i<100000ULL; ++i) {
        if (atomic_u64_load(&ipc_send_probe_state)>=2)
            break;
        scheduler_yield();
    }
    if (atomic_u64_load(&ipc_send_probe_state)!=2 ||
        thread->state!=THREAD_ZOMBIE ||
        thread_reap(thread,&status)!=0 || status!=0 ||
        ipc_send_probe_process->state!=PROCESS_ZOMBIE ||
        vmm_activate_kernel()!=0 ||
        process_reap(ipc_send_probe_process,&status)!=0 || status!=0 ||
        process_lookup(pid)!=0 || ipc_debug_validate()!=0) {
        thread=0;
        goto fail;
    }
    thread=0;
    thread_created=0;
    ipc_send_probe_process=0;
    ipc_send_probe_signal=0;
    ipc_send_probe_wait=0;
    return 0;

release_sender:
    /* Closing the sender cancels a failed blocked-send observation, allowing
     * the temporary process to follow the ordinary zombie/reap path. */
    if (ipc_send_probe_process && ipc_send_probe_signal) {
        (void)ipc_close(ipc_send_probe_process,ipc_send_probe_signal);
        ipc_send_probe_signal=0;
    }
    for (uint64_t i=0; i<100000ULL &&
         atomic_u64_load(&ipc_send_probe_state)<2; ++i)
        scheduler_yield();
fail:
    if (thread_created && thread && thread->state==THREAD_ZOMBIE) {
        (void)thread_reap(thread,&status);
        thread=0;
    }
    if (ipc_send_probe_process &&
        ipc_send_probe_process->state==PROCESS_ZOMBIE) {
        (void)vmm_activate_kernel();
        (void)process_reap(ipc_send_probe_process,&status);
    } else if (ipc_send_probe_process &&
               ipc_send_probe_process->state==PROCESS_NEW) {
        if (ipc_send_probe_signal)
            (void)ipc_close(ipc_send_probe_process,ipc_send_probe_signal);
        if (ipc_send_probe_wait)
            (void)ipc_close(ipc_send_probe_process,ipc_send_probe_wait);
        (void)process_abort_new(ipc_send_probe_process);
    }
    ipc_send_probe_process=0;
    ipc_send_probe_signal=0;
    ipc_send_probe_wait=0;
    return result;
}

static int userspace_resource_self_test(struct process *process) {
    struct zeroos_ipc_pair pairs[ZEROOS_IPC_MAX_ENDPOINTS/2U];
    zeroos_shmem_handle_t shmem_handles[ZEROOS_SHMEM_MAX_OBJECTS];
    process_id_t first_pid=0;
    process_id_t second_pid=0;
    struct process *first=0;
    struct process *second=0;
    uint32_t pair_count=0;
    uint32_t shmem_count=0;
    int result=-1;

    if (!process || process_set_limits(process,4,1,16)!=0)
        return -1;
    if (process_create(process,&first_pid)!=0)
        goto restore_limit;
    first=process_lookup(first_pid);
    if (!first || first->state!=PROCESS_NEW)
        goto cleanup_children;
    if (process_create(process,&second_pid)==0) {
        second=process_lookup(second_pid);
        goto cleanup_children;
    }
    if (process_abort_new(first)!=0)
        goto restore_limit;
    first=0;
    if (process_set_limits(process,4,4,16)!=0)
        return -1;

    while (pair_count<ZEROOS_IPC_MAX_ENDPOINTS/2U) {
        if (ipc_create(process,&pairs[pair_count].local,
                       &pairs[pair_count].peer)!=0)
            goto cleanup_ipc;
        ++pair_count;
    }
    {
        zeroos_ipc_handle_t extra_local=0;
        zeroos_ipc_handle_t extra_peer=0;
        if (ipc_create(process,&extra_local,&extra_peer)!=-ZEROOS_ENOMEM)
            goto cleanup_ipc;
    }
    while (shmem_count<ZEROOS_SHMEM_MAX_OBJECTS) {
        if (shmem_create(process,VMM_PAGE_SIZE,0,
                         &shmem_handles[shmem_count])!=0)
            goto cleanup_shmem;
        ++shmem_count;
    }
    {
        zeroos_shmem_handle_t extra=0;
        if (shmem_create(process,VMM_PAGE_SIZE,0,&extra)!=-ZEROOS_ENOMEM) {
            if (extra)
                (void)shmem_close(process,extra);
            goto cleanup_shmem;
        }
    }
    result=0;

cleanup_shmem:
    while (shmem_count) {
        --shmem_count;
        (void)shmem_close(process,shmem_handles[shmem_count]);
    }
    if (result==0 && shmem_debug_validate()!=0)
        result=-1;

cleanup_ipc:
    while (pair_count) {
        --pair_count;
        (void)ipc_close(process,pairs[pair_count].local);
        (void)ipc_close(process,pairs[pair_count].peer);
    }
    if (result==0 && ipc_debug_validate()!=0)
        result=-1;
    return result;

cleanup_children:
    if (second && second->state==PROCESS_NEW)
        (void)process_abort_new(second);
    if (first && first->state==PROCESS_NEW)
        (void)process_abort_new(first);
restore_limit:
    (void)process_set_limits(process,4,4,16);
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
    uint64_t request_length;
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
        request_length=sizeof(service_request_one)-1U;
    } else {
        message=service_message_two;
        message_length=sizeof(service_message_two)-1U;
        request_length=sizeof(service_request_two)-1U;
    }
    image_size=build_service_elf(0,0,message,message_length,
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
        ipc_grant_rights(controller,controller_peer,worker->pid,
                         ZEROOS_IPC_RIGHT_SEND|ZEROOS_IPC_RIGHT_RECV|
                         ZEROOS_IPC_RIGHT_CLOSE,
                         &worker_handle)!=0)
        goto fail;
    image_size=build_service_elf(worker_handle,controller->pid,message,
                                 message_length,attempt==1 ? 7U : 0U);
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
    service_request_length=request_length;
    service_request_sent=0;
    service_started=1;
    if (attempt==1)
        serial_write_public("ZEROOS: service capability least-privilege grant passed.\n");
    if (attempt==1)
        serial_write_public("ZEROOS: service manager launched isolated IPC service attempt 1.\n");
    else
        serial_write_public("ZEROOS: service manager launched isolated IPC service attempt 2.\n");
    serial_write_public("ZEROOS: isolated service process setup complete.\n");
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
        !service_thread)
        return -1;
    if (service_thread->state!=THREAD_ZOMBIE) {
        struct task *task=task_lookup(service_thread->scheduler_task_id);
        if (!service_request_sent && task && task->state==TASK_BLOCKED) {
            const char *request=service_attempt==1 ?
                               service_request_one : service_request_two;
            if (ipc_send(service_controller,service_controller_handle,
                         request,service_request_length,
                         ZEROOS_IPC_FLAG_NONBLOCK)!=
                    (int)service_request_length)
                return -1;
            service_request_sent=1;
            serial_write_public("ZEROOS: blocking IPC wait/wake path passed.\n");
        }
        return 0;
    }
    receive_result=ipc_receive(service_controller,service_controller_handle,
                               receive_buffer,sizeof(receive_buffer),
                               ZEROOS_IPC_FLAG_NONBLOCK,&received_length);
    if (receive_result<0 || (uint64_t)receive_result!=service_expected_length ||
        received_length!=service_expected_length) {
        if (service_thread->state==THREAD_ZOMBIE) {
            if (service_thread->exit_status==9)
                serial_write_public("ZEROOS PANIC: service worker exact grant-denial assertion failed.\n");
            else
                serial_write_public("ZEROOS PANIC: isolated service exited before IPC reply.\n");
        }
        return -1;
    }
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
        serial_write_public("ZEROOS: service worker grant denial passed.\n");
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
        shmem_debug_validate()!=0 ||
        elf_debug_validate()!=0 || elf_system_init()!=0 ||
        exec_system_init()!=0 || exec_debug_validate()!=0)
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
    service_request_length=0;
    service_request_sent=0;
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
    serial_write_public("ZEROOS: event and pipe IPC foundations self-test passed.\n");
    if (userspace_event_blocking_self_test()!=0) {
        serial_write_public("ZEROOS PANIC: blocking event wait/wake self-test failed.\n");
        goto fail;
    }
    serial_write_public("ZEROOS: blocking event wait/wake path passed.\n");
    if (userspace_ipc_close_wakeup_self_test()!=0) {
        serial_write_public("ZEROOS PANIC: blocking IPC close-wakeup self-test failed.\n");
        goto fail;
    }
    serial_write_public("ZEROOS: blocking IPC close-wakeup path passed.\n");
    if (userspace_ipc_send_wakeup_self_test()!=0) {
        serial_write_public("ZEROOS PANIC: blocking IPC send-wakeup self-test failed.\n");
        goto fail;
    }
    serial_write_public("ZEROOS: blocking IPC send-wakeup path passed.\n");
    if (userspace_shmem_self_test(init_process)!=0) {
        serial_write_public("ZEROOS PANIC: shared-memory lifecycle self-test failed.\n");
        goto fail;
    }
    serial_write_public("ZEROOS: shared-memory map/grant/lifecycle self-test passed.\n");
    if (userspace_resource_self_test(init_process)!=0) {
        serial_write_public("ZEROOS PANIC: userspace resource exhaustion self-test failed.\n");
        goto fail;
    }
    serial_write_public("ZEROOS: userspace resource exhaustion/recovery passed.\n");

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
    if (status!=0)
        return -1;
    init_reaped=1;
    serial_write_public("ZEROOS: userspace negative syscall/fault/malformed-ELF probes passed.\n");
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

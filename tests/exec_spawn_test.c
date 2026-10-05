#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "elf.h"
#include "exec.h"
#include "memory.h"
#include "process.h"
#include "sync.h"
#include "syscall.h"
#include "thread.h"
#include "user.h"
#include "vmm.h"

#define FAKE_USER_BASE 0x1000ULL
#define FAKE_USER_MEMORY_SIZE 8192ULL
#define TEST_CHILD_PID 42ULL
#define TEST_CHILD_TID 7ULL
#define TEST_ENTRY (ZEROOS_USER_CODE_BASE + 0x1000ULL)
#define TEST_PHDR (ZEROOS_USER_CODE_BASE + 0x3000ULL)

static struct process parent_process;
static struct process child_process;
static uint8_t fake_user_memory[FAKE_USER_MEMORY_SIZE];
static uint8_t fake_stack_page[VMM_PAGE_SIZE];

static uint64_t copy_calls;
static struct process *last_copy_process;
static uint64_t create_calls;
static uint64_t set_limits_calls;
static uint64_t elf_load_calls;
static uint64_t elf_unload_calls;
static uint64_t page_alloc_calls;
static uint64_t page_free_calls;
static uint64_t map_calls;
static uint64_t unmap_calls;
static uint64_t abort_calls;
static uint64_t thread_create_calls;
static uint64_t last_user_entry;
static uint64_t last_user_stack;
static int copy_error;
static int create_result;
static int elf_load_result;
static int map_result;
static int abort_result;
static int thread_create_result;

static void reset_fixture(void) {
    struct zeroos_elf64_ehdr header={0};

    memset(&parent_process,0,sizeof(parent_process));
    memset(&child_process,0,sizeof(child_process));
    memset(fake_user_memory,0,sizeof(fake_user_memory));
    memset(fake_stack_page,0,sizeof(fake_stack_page));
    parent_process.pid=1;
    parent_process.state=PROCESS_RUNNING;

    copy_calls=0;
    last_copy_process=0;
    create_calls=0;
    set_limits_calls=0;
    elf_load_calls=0;
    elf_unload_calls=0;
    page_alloc_calls=0;
    page_free_calls=0;
    map_calls=0;
    unmap_calls=0;
    abort_calls=0;
    thread_create_calls=0;
    last_user_entry=0;
    last_user_stack=0;
    copy_error=0;
    create_result=0;
    elf_load_result=0;
    map_result=0;
    abort_result=0;
    thread_create_result=-1;

    header.phentsize=sizeof(struct zeroos_elf64_phdr);
    header.phnum=1;
    memcpy(fake_user_memory,&header,sizeof(header));
}

static void add_argument(const char *argument) {
    uint64_t argument_address=FAKE_USER_BASE+0x300ULL;
    size_t length=strlen(argument)+1U;

    assert(length<=FAKE_USER_MEMORY_SIZE-0x300ULL);
    memcpy(fake_user_memory+0x100ULL,&argument_address,
           sizeof(argument_address));
    memcpy(fake_user_memory+0x300ULL,argument,length);
}

static int run_spawn(uint64_t user_argv, uint64_t argc,
                     struct zeroos_exec_spawn_result *result) {
    return exec_spawn(&parent_process,FAKE_USER_BASE,
                      sizeof(struct zeroos_elf64_ehdr),user_argv,argc,
                      0,0,result);
}

/* Minimal, deterministic kernel-service fakes for the exec transaction. */
void spinlock_init(struct spinlock *lock) {
    lock->value=0;
    lock->contention=0;
}

uint64_t spin_lock_irqsave(struct spinlock *lock) {
    assert(lock);
    assert(lock->value==0);
    lock->value=1;
    return 0x5a5aULL;
}

void spin_unlock_irqrestore(struct spinlock *lock, uint64_t flags) {
    assert(lock);
    assert(flags==0x5a5aULL);
    assert(lock->value==1);
    lock->value=0;
}

int process_address_space_copy_from_user(struct process *process,
                                         void *destination,
                                         uint64_t source,
                                         uint64_t length) {
    uint64_t offset;

    ++copy_calls;
    last_copy_process=process;
    if (copy_error || !process || !destination || source<FAKE_USER_BASE)
        return -1;
    offset=source-FAKE_USER_BASE;
    if (offset>FAKE_USER_MEMORY_SIZE ||
        length>FAKE_USER_MEMORY_SIZE-offset)
        return -1;
    memcpy(destination,fake_user_memory+offset,(size_t)length);
    return 0;
}

int process_create(struct process *parent, process_id_t *pid_out) {
    ++create_calls;
    assert(parent==&parent_process);
    if (create_result!=0)
        return create_result;
    memset(&child_process,0,sizeof(child_process));
    child_process.pid=TEST_CHILD_PID;
    child_process.parent=parent;
    child_process.state=PROCESS_NEW;
    *pid_out=TEST_CHILD_PID;
    return 0;
}

struct process *process_lookup(process_id_t pid) {
    return pid==TEST_CHILD_PID ? &child_process : 0;
}

int process_set_limits(struct process *process, uint64_t max_threads,
                       uint64_t max_children,
                       uint64_t max_address_space_pages) {
    ++set_limits_calls;
    assert(process==&child_process);
    assert(max_threads==4 && max_children==4);
    assert(max_address_space_pages==64);
    return 0;
}

int process_address_space_map_page(struct process *process,
                                   uint64_t virtual_address,
                                   uint64_t physical_address,
                                   uint64_t flags) {
    ++map_calls;
    assert(process==&child_process);
    assert(virtual_address==ZEROOS_USER_STACK_PAGE);
    assert(physical_address==(uint64_t)(uintptr_t)fake_stack_page);
    assert(flags==(VMM_USER|VMM_WRITABLE|VMM_NO_EXECUTE));
    return map_result;
}

int process_address_space_unmap_page(struct process *process,
                                     uint64_t virtual_address) {
    ++unmap_calls;
    assert(process==&child_process);
    assert(virtual_address==ZEROOS_USER_STACK_PAGE);
    return 0;
}

int process_abort_new(struct process *process) {
    ++abort_calls;
    assert(process==&child_process);
    if (abort_result==0)
        process->state=PROCESS_UNUSED;
    return abort_result;
}

int elf_load_image(struct process *process, const void *image,
                   uint64_t image_size,
                   struct zeroos_elf_load_result *result) {
    const struct zeroos_elf64_ehdr *header=image;

    ++elf_load_calls;
    assert(process==&child_process);
    assert(image_size==sizeof(*header));
    assert(header->phnum==1);
    if (elf_load_result==0) {
        result->entry=TEST_ENTRY;
        result->program_header_address=TEST_PHDR;
        result->mapped_pages=1;
    }
    return elf_load_result;
}

int elf_unload_image(struct process *process, const void *image,
                     uint64_t image_size) {
    ++elf_unload_calls;
    assert(process==&child_process);
    assert(image);
    assert(image_size==sizeof(struct zeroos_elf64_ehdr));
    return 0;
}

void *page_alloc_zero(void) {
    ++page_alloc_calls;
    return fake_stack_page;
}

void page_free(void *address) {
    ++page_free_calls;
    assert(address==fake_stack_page);
}

int thread_create_user(struct process *process, uint64_t user_entry,
                       uint64_t user_stack, thread_id_t *tid_out) {
    ++thread_create_calls;
    assert(process==&child_process);
    last_user_entry=user_entry;
    last_user_stack=user_stack;
    if (thread_create_result==0)
        *tid_out=TEST_CHILD_TID;
    return thread_create_result;
}

static void test_copy_failure_is_efault_and_creates_no_child(void) {
    struct zeroos_exec_spawn_result result={99,88};

    reset_fixture();
    copy_error=1;
    assert(run_spawn(0,0,&result)==-ZEROOS_EFAULT);
    assert(result.pid==0 && result.tid==0);
    assert(copy_calls==1 && last_copy_process==&parent_process);
    assert(create_calls==0 && elf_load_calls==0 && abort_calls==0);
}

static void test_thread_failure_rolls_back_and_returns_error(void) {
    struct zeroos_exec_spawn_result result={99,88};

    reset_fixture();
    add_argument("arg");
    assert(run_spawn(FAKE_USER_BASE+0x100ULL,1,&result)==-ZEROOS_ENOMEM);
    assert(result.pid==0 && result.tid==0);
    assert(copy_calls==6 && last_copy_process==&parent_process);
    assert(create_calls==1 && set_limits_calls==1 && elf_load_calls==1);
    assert(page_alloc_calls==1 && page_free_calls==1 && map_calls==1);
    assert(unmap_calls==1 && elf_unload_calls==1 && abort_calls==1);
    assert(thread_create_calls==1);
    assert(child_process.state==PROCESS_UNUSED);
}

static void test_success_publishes_pid_tid_and_stack(void) {
    struct zeroos_exec_spawn_result result={99,88};
    uint64_t stack_index;
    uint64_t initial_word;

    reset_fixture();
    add_argument("arg");
    thread_create_result=0;
    assert(run_spawn(FAKE_USER_BASE+0x100ULL,1,&result)==0);
    assert(result.pid==TEST_CHILD_PID && result.tid==TEST_CHILD_TID);
    assert(copy_calls==6 && create_calls==1 && elf_load_calls==1);
    assert(thread_create_calls==1 && last_user_entry==TEST_ENTRY);
    assert(last_user_stack>=ZEROOS_USER_STACK_PAGE);
    assert(last_user_stack<ZEROOS_USER_STACK_PAGE+VMM_PAGE_SIZE);
    assert((last_user_stack&0xfULL)==0);
    assert(unmap_calls==0 && elf_unload_calls==0 && abort_calls==0);
    assert(page_free_calls==1);

    stack_index=last_user_stack-ZEROOS_USER_STACK_PAGE;
    memcpy(&initial_word,fake_stack_page+stack_index,sizeof(initial_word));
    assert(initial_word==1);
    memcpy(&initial_word,fake_stack_page+stack_index+sizeof(uint64_t),
           sizeof(initial_word));
    assert(initial_word>=ZEROOS_USER_STACK_PAGE &&
           initial_word<ZEROOS_USER_STACK_PAGE+VMM_PAGE_SIZE);
}

static void test_loader_failure_aborts_unpublished_child(void) {
    struct zeroos_exec_spawn_result result={99,88};

    reset_fixture();
    elf_load_result=-ZEROOS_EINVAL;
    assert(run_spawn(0,0,&result)==-ZEROOS_EINVAL);
    assert(result.pid==0 && result.tid==0);
    assert(create_calls==1 && elf_load_calls==1);
    assert(page_alloc_calls==0 && elf_unload_calls==0);
    assert(abort_calls==1 && child_process.state==PROCESS_UNUSED);
}

static void test_mapping_failure_releases_stack_and_child(void) {
    struct zeroos_exec_spawn_result result={99,88};

    reset_fixture();
    map_result=-1;
    assert(run_spawn(0,0,&result)==-ZEROOS_ENOMEM);
    assert(result.pid==0 && result.tid==0);
    assert(page_alloc_calls==1 && page_free_calls==1 && map_calls==1);
    assert(unmap_calls==0 && elf_unload_calls==1 && abort_calls==1);
    assert(child_process.state==PROCESS_UNUSED);
}

static void test_abort_failure_is_reported_as_io_error(void) {
    struct zeroos_exec_spawn_result result={99,88};

    reset_fixture();
    abort_result=-1;
    assert(run_spawn(0,0,&result)==-ZEROOS_EIO);
    assert(result.pid==0 && result.tid==0);
    assert(thread_create_calls==1 && abort_calls==1);
    assert(child_process.state==PROCESS_NEW);
}

int main(void) {
    assert(exec_system_init()==0);
    test_copy_failure_is_efault_and_creates_no_child();
    test_thread_failure_rolls_back_and_returns_error();
    test_success_publishes_pid_tid_and_stack();
    test_loader_failure_aborts_unpublished_child();
    test_mapping_failure_releases_stack_and_child();
    test_abort_failure_is_reported_as_io_error();
    puts("exec_spawn_test: PASS (6 transaction and user-copy scenarios)");
    return 0;
}

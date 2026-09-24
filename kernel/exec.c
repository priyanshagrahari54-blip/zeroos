#include "exec.h"
#include "elf.h"
#include "memory.h"
#include "process.h"
#include "thread.h"
#include "sync.h"
#include "syscall.h"
#include "user.h"
#include "vmm.h"

struct exec_workspace {
    struct spinlock lock;
    uint8_t initialized;
    uint8_t image[ZEROOS_EXEC_MAX_IMAGE];
    uint8_t stack[VMM_PAGE_SIZE];
    uint8_t strings[ZEROOS_EXEC_MAX_STRING_BYTES];
    uint64_t argument_offsets[ZEROOS_EXEC_MAX_ARGUMENTS];
    uint64_t environment_offsets[ZEROOS_EXEC_MAX_ARGUMENTS];
    uint64_t argument_addresses[ZEROOS_EXEC_MAX_ARGUMENTS];
    uint64_t environment_addresses[ZEROOS_EXEC_MAX_ARGUMENTS];
    uint64_t string_bytes;
};

static struct exec_workspace exec_workspace;

#define ZEROOS_AUXV_NULL    0ULL
#define ZEROOS_AUXV_PHDR    3ULL
#define ZEROOS_AUXV_PHENT   4ULL
#define ZEROOS_AUXV_PHNUM   5ULL
#define ZEROOS_AUXV_PAGESZ  6ULL
#define ZEROOS_AUXV_BASE    7ULL
#define ZEROOS_AUXV_ENTRY   9ULL

static int exec_copy_from_user(const struct process *process,
                               uint64_t source, void *destination,
                               uint64_t length) {
    uint8_t *out=(uint8_t *)destination;
    uint64_t offset=0;

    if (!process || !destination ||
        (length && !process_address_space_is_user_range(process,source,
                                                        length,0)))
        return -ZEROOS_EFAULT;
    while (offset<length) {
        uint64_t address=source+offset;
        uint64_t physical=vmm_space_translate(&process->address_space,address);
        uint64_t within=VMM_PAGE_SIZE-(address&(VMM_PAGE_SIZE-1ULL));
        uint64_t count=length-offset<within ? length-offset : within;
        if (!physical)
            return -ZEROOS_EFAULT;
        for (uint64_t i=0; i<count; ++i)
            out[offset+i]=((const uint8_t *)(uint64_t)(physical+i))[0];
        offset+=count;
    }
    return 0;
}

static int exec_copy_string(const struct process *process, uint64_t source,
                            uint64_t offset, uint64_t *length_out) {
    uint64_t length=0;
    if (!source || offset>=ZEROOS_EXEC_MAX_STRING_BYTES)
        return -ZEROOS_EFAULT;
    while (length<ZEROOS_EXEC_MAX_STRING) {
        uint8_t byte=0;
        int result=exec_copy_from_user(process,source+length,&byte,1);
        if (result!=0)
            return result;
        if (offset+length>=ZEROOS_EXEC_MAX_STRING_BYTES)
            return -ZEROOS_E2BIG;
        exec_workspace.strings[offset+length]=byte;
        ++length;
        if (byte==0) {
            *length_out=length;
            return 0;
        }
    }
    return -ZEROOS_E2BIG;
}

static int exec_stack_push(uint64_t *cursor, uint64_t value) {
    if (!cursor || *cursor<sizeof(value))
        return -ZEROOS_E2BIG;
    *cursor-=sizeof(value);
    for (uint32_t i=0; i<sizeof(value); ++i)
        exec_workspace.stack[*cursor+i]=(uint8_t)(value>>(i*8U));
    return 0;
}

static int exec_stack_copy_string(uint64_t *cursor, uint64_t source_offset,
                                  uint64_t length, uint64_t *address_out) {
    if (!cursor || !address_out || length>*cursor)
        return -ZEROOS_E2BIG;
    *cursor-=length;
    for (uint64_t i=0; i<length; ++i)
        exec_workspace.stack[*cursor+i]=
            exec_workspace.strings[source_offset+i];
    *address_out=ZEROOS_USER_STACK_PAGE+*cursor;
    return 0;
}

static int exec_build_stack(const struct zeroos_elf_load_result *load_result,
                            uint64_t argc, uint64_t envc,
                            const struct zeroos_elf64_ehdr *header,
                            uint64_t *stack_pointer_out) {
    uint64_t cursor=VMM_PAGE_SIZE;
    uint64_t auxiliary_words=16ULL;
    uint64_t total_words=auxiliary_words+envc+1ULL+argc+1ULL+1ULL;

    for (uint64_t i=0; i<VMM_PAGE_SIZE; ++i)
        exec_workspace.stack[i]=0;
    for (uint64_t i=0; i<envc; ++i) {
        uint64_t offset=exec_workspace.environment_offsets[i];
        uint64_t length=0;
        while (offset+length<exec_workspace.string_bytes &&
               exec_workspace.strings[offset+length]!=0)
            ++length;
        ++length;
        if (exec_stack_copy_string(&cursor,offset,length,
                                   &exec_workspace.environment_addresses[i])!=0)
            return -ZEROOS_E2BIG;
    }
    /* The strings were copied in forward order; their exact order in memory
     * is irrelevant because the vectors below retain each address. Re-copy
     * argument strings from the same immutable workspace after env strings. */
    for (uint64_t i=0; i<argc; ++i) {
        uint64_t offset=exec_workspace.argument_offsets[i];
        uint64_t length=0;
        while (offset+length<exec_workspace.string_bytes &&
               exec_workspace.strings[offset+length]!=0)
            ++length;
        ++length;
        if (exec_stack_copy_string(&cursor,offset,length,
                                   &exec_workspace.argument_addresses[i])!=0)
            return -ZEROOS_E2BIG;
    }

    cursor&=~0xfULL;
    if (total_words&1ULL) {
        if (cursor<sizeof(uint64_t))
            return -ZEROOS_E2BIG;
        cursor-=sizeof(uint64_t);
    }

    /* Push the auxiliary vector from high address to low address. AT_PHDR is
     * zero in this static-loader ABI because there is no PT_PHDR contract. */
    if (exec_stack_push(&cursor,0) != 0 ||
        exec_stack_push(&cursor,ZEROOS_AUXV_NULL) != 0 ||
        exec_stack_push(&cursor,load_result->entry) != 0 ||
        exec_stack_push(&cursor,ZEROOS_AUXV_ENTRY) != 0 ||
        exec_stack_push(&cursor,0) != 0 ||
        exec_stack_push(&cursor,ZEROOS_AUXV_BASE) != 0 ||
        exec_stack_push(&cursor,VMM_PAGE_SIZE) != 0 ||
        exec_stack_push(&cursor,ZEROOS_AUXV_PAGESZ) != 0 ||
        exec_stack_push(&cursor,header->phnum) != 0 ||
        exec_stack_push(&cursor,ZEROOS_AUXV_PHNUM) != 0 ||
        exec_stack_push(&cursor,header->phentsize) != 0 ||
        exec_stack_push(&cursor,ZEROOS_AUXV_PHENT) != 0 ||
        exec_stack_push(&cursor,0) != 0 ||
        exec_stack_push(&cursor,ZEROOS_AUXV_PHDR) != 0)
        return -ZEROOS_E2BIG;

    if (exec_stack_push(&cursor,0)!=0)
        return -ZEROOS_E2BIG;
    for (uint64_t i=envc; i>0; --i)
        if (exec_stack_push(&cursor,exec_workspace.environment_addresses[i-1])!=0)
            return -ZEROOS_E2BIG;
    if (exec_stack_push(&cursor,0)!=0)
        return -ZEROOS_E2BIG;
    for (uint64_t i=argc; i>0; --i)
        if (exec_stack_push(&cursor,exec_workspace.argument_addresses[i-1])!=0)
            return -ZEROOS_E2BIG;
    if (exec_stack_push(&cursor,argc)!=0)
        return -ZEROOS_E2BIG;

    *stack_pointer_out=ZEROOS_USER_STACK_PAGE+cursor;
    return 0;
}

static int exec_copy_vectors(const struct process *parent,
                             uint64_t user_vector, uint64_t count,
                             uint64_t *offsets_out) {
    for (uint64_t i=0; i<count; ++i) {
        uint64_t pointer=0;
        int result=exec_copy_from_user(parent,
                                       user_vector+i*sizeof(pointer),
                                       &pointer,sizeof(pointer));
        uint64_t length=0;
        if (result!=0)
            return result;
        if (exec_workspace.string_bytes>=ZEROOS_EXEC_MAX_STRING_BYTES)
            return -ZEROOS_E2BIG;
        result=exec_copy_string(parent,pointer,exec_workspace.string_bytes,
                                &length);
        if (result!=0)
            return result;
        offsets_out[i]=exec_workspace.string_bytes;
        exec_workspace.string_bytes+=length;
    }
    return 0;
}

int exec_system_init(void) {
    spinlock_init(&exec_workspace.lock);
    exec_workspace.initialized=1;
    exec_workspace.string_bytes=0;
    return 0;
}

int exec_spawn(struct process *parent, uint64_t user_image,
               uint64_t image_size, uint64_t user_argv, uint64_t argc,
               uint64_t user_envp, uint64_t envc,
               struct zeroos_exec_spawn_result *result) {
    uint64_t lock_flags;
    process_id_t pid=0;
    thread_id_t tid=0;
    struct process *child=0;
    struct zeroos_elf_load_result load_result={0};
    const struct zeroos_elf64_ehdr *header;
    void *stack_page=0;
    uint64_t stack_pointer=0;
    uint8_t image_loaded=0;
    uint8_t stack_mapped=0;
    int copy_result;
    int failure_result=-ZEROOS_ENOMEM;

    if (!exec_workspace.initialized || !parent || !result ||
        image_size==0 || image_size>ZEROOS_EXEC_MAX_IMAGE ||
        argc>ZEROOS_EXEC_MAX_ARGUMENTS || envc>ZEROOS_EXEC_MAX_ARGUMENTS ||
        (argc && !user_argv) || (envc && !user_envp))
        return -ZEROOS_EINVAL;
    if ((argc && user_argv>~0ULL/sizeof(uint64_t)) ||
        (envc && user_envp>~0ULL/sizeof(uint64_t)))
        return -ZEROOS_EFAULT;

    lock_flags=spin_lock_irqsave(&exec_workspace.lock);
    exec_workspace.string_bytes=0;
    copy_result=exec_copy_from_user(parent,user_image,exec_workspace.image,
                                    image_size);
    if (copy_result!=0) {
        failure_result=copy_result;
        goto fail_locked;
    }
    copy_result=exec_copy_vectors(parent,user_argv,argc,
                                  exec_workspace.argument_offsets);
    if (copy_result!=0) {
        failure_result=copy_result;
        goto fail_locked;
    }
    copy_result=exec_copy_vectors(parent,user_envp,envc,
                                  exec_workspace.environment_offsets);
    if (copy_result!=0) {
        failure_result=copy_result;
        goto fail_locked;
    }

    header=(const struct zeroos_elf64_ehdr *)exec_workspace.image;
    if (image_size<sizeof(*header) || header->phnum==0) {
        failure_result=-ZEROOS_EINVAL;
        goto fail_locked;
    }

    if (process_create(parent,&pid)!=0)
        goto fail_locked;
    child=process_lookup(pid);
    if (!child || process_set_limits(child,4,4,64)!=0)
        goto fail_locked;
    failure_result=elf_load_image(child,exec_workspace.image,image_size,&load_result);
    if (failure_result!=0)
        goto fail_locked;
    image_loaded=1;

    stack_page=page_alloc_zero();
    if (!stack_page) {
        failure_result=-ZEROOS_ENOMEM;
        goto fail_locked;
    }
    if (process_address_space_map_page(child,ZEROOS_USER_STACK_PAGE,
                                       (uint64_t)stack_page,
                                       VMM_USER|VMM_WRITABLE|VMM_NO_EXECUTE)!=0) {
        failure_result=-ZEROOS_ENOMEM;
        goto fail_locked;
    }
    stack_mapped=1;
    for (uint64_t i=0; i<VMM_PAGE_SIZE; ++i)
        ((uint8_t *)(uint64_t)stack_page)[i]=0;
    failure_result=exec_build_stack(&load_result,argc,envc,header,&stack_pointer);
    if (failure_result!=0)
        goto fail_locked;
    for (uint64_t i=0; i<VMM_PAGE_SIZE; ++i)
        ((uint8_t *)(uint64_t)stack_page)[i]=exec_workspace.stack[i];
    page_free(stack_page);
    stack_page=0;
    stack_mapped=1;

    if (thread_create_user(child,load_result.entry,stack_pointer,&tid)!=0)
        goto fail_locked;
    result->pid=pid;
    result->tid=tid;
    spin_unlock_irqrestore(&exec_workspace.lock,lock_flags);
    return 0;

fail_locked:
    if (stack_page)
        page_free(stack_page);
    if (stack_mapped && child)
        (void)process_address_space_unmap_page(child,ZEROOS_USER_STACK_PAGE);
    if (image_loaded && child)
        (void)elf_unload_image(child,exec_workspace.image,image_size);
    if (child && child->state==PROCESS_NEW)
        (void)process_abort_new(child);
    spin_unlock_irqrestore(&exec_workspace.lock,lock_flags);
    return failure_result;
}

int exec_debug_validate(void) {
    if (!exec_workspace.initialized ||
        ZEROOS_EXEC_MAX_ARGUMENTS==0 ||
        ZEROOS_EXEC_MAX_STRING<2 ||
        ZEROOS_EXEC_MAX_IMAGE<VMM_PAGE_SIZE)
        return -1;
    return 0;
}

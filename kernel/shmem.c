#include "shmem.h"
#include "memory.h"
#include "process.h"
#include "sync.h"
#include "user.h"
#include "vmm.h"
#include "syscall.h"

struct shmem_object {
    uint32_t generation;
    uint8_t used;
    uint8_t reserved[3];
    uint32_t page_count;
    uint32_t cap_refs;
    uint32_t mapping_refs;
    uint32_t operation_refs;
    uint64_t pages[ZEROOS_SHMEM_MAX_PAGES];
};

struct shmem_capability {
    uint32_t generation;
    uint8_t used;
    uint8_t rights;
    uint16_t reserved;
    struct process *owner;
    struct shmem_object *object;
};

struct shmem_mapping {
    uint8_t used;
    uint8_t reserved[3];
    struct process *owner;
    struct shmem_object *object;
    uint64_t virtual_address;
    uint32_t page_count;
};

static struct spinlock shmem_lock;
static struct shmem_object objects[ZEROOS_SHMEM_MAX_OBJECTS];
static struct shmem_capability capabilities[ZEROOS_SHMEM_MAX_CAPABILITIES];
static struct shmem_mapping mappings[ZEROOS_SHMEM_MAX_MAPPINGS];

static int process_can_use(const struct process *process) {
    return process && process->state!=PROCESS_UNUSED &&
           process->state!=PROCESS_ZOMBIE;
}

static uint64_t capability_make_handle(uint32_t slot, uint32_t generation) {
    return ((uint64_t)generation << 8) | ((uint64_t)slot + 1ULL);
}

static int capability_decode(zeroos_shmem_handle_t handle,
                             uint32_t *slot_out, uint32_t *generation_out) {
    uint32_t encoded_slot=(uint32_t)(handle & 0xffULL);
    uint64_t generation=handle >> 8;
    if (encoded_slot==0 || encoded_slot>ZEROOS_SHMEM_MAX_CAPABILITIES ||
        generation==0 || generation>0xffffffffULL)
        return -1;
    *slot_out=encoded_slot-1U;
    *generation_out=(uint32_t)generation;
    return 0;
}

static struct shmem_capability *capability_lookup_locked(
        struct process *owner, zeroos_shmem_handle_t handle) {
    uint32_t slot;
    uint32_t generation;
    if (!process_can_use(owner) ||
        capability_decode(handle,&slot,&generation)!=0)
        return 0;
    struct shmem_capability *capability=&capabilities[slot];
    if (!capability->used || capability->generation!=generation ||
        capability->owner!=owner || !capability->object ||
        !capability->object->used)
        return 0;
    return capability;
}

static void object_destroy_locked(struct shmem_object *object) {
    if (!object || !object->used || object->cap_refs ||
        object->mapping_refs || object->operation_refs)
        return;
    for (uint32_t i=0; i<object->page_count; ++i)
        page_free((void *)(uint64_t)object->pages[i]);
    object->used=0;
    object->page_count=0;
    object->cap_refs=0;
    object->mapping_refs=0;
    object->operation_refs=0;
    for (uint32_t i=0; i<ZEROOS_SHMEM_MAX_PAGES; ++i)
        object->pages[i]=0;
}

static void capability_drop_locked(struct shmem_capability *capability) {
    struct shmem_object *object;
    if (!capability || !capability->used)
        return;
    object=capability->object;
    capability->used=0;
    capability->owner=0;
    capability->object=0;
    capability->rights=0;
    if (object && object->used && object->cap_refs)
        --object->cap_refs;
    object_destroy_locked(object);
}

static int capability_alloc_locked(struct process *owner,
                                   struct shmem_object *object,
                                   uint8_t rights,
                                   zeroos_shmem_handle_t *handle_out) {
    for (uint32_t i=0; i<ZEROOS_SHMEM_MAX_CAPABILITIES; ++i) {
        struct shmem_capability *capability=&capabilities[i];
        if (capability->used || capability->generation==0xffffffffU)
            continue;
        ++capability->generation;
        if (capability->generation==0)
            continue;
        capability->used=1;
        capability->rights=rights;
        capability->owner=owner;
        capability->object=object;
        ++object->cap_refs;
        if (handle_out)
            *handle_out=capability_make_handle(i,capability->generation);
        return 0;
    }
    return -ZEROOS_ENOMEM;
}

static int object_page_count_for_size(uint64_t size, uint32_t *count_out) {
    uint64_t count;
    if (!size || size>ZEROOS_SHMEM_MAX_SIZE ||
        size>~0ULL-(VMM_PAGE_SIZE-1ULL))
        return -ZEROOS_EINVAL;
    count=(size+VMM_PAGE_SIZE-1ULL)/VMM_PAGE_SIZE;
    if (!count || count>ZEROOS_SHMEM_MAX_PAGES)
        return -ZEROOS_EINVAL;
    *count_out=(uint32_t)count;
    return 0;
}

int shmem_system_init(void) {
    spinlock_init(&shmem_lock);
    for (uint32_t i=0; i<ZEROOS_SHMEM_MAX_OBJECTS; ++i) {
        objects[i].generation=0;
        objects[i].used=0;
        objects[i].page_count=0;
        objects[i].cap_refs=0;
        objects[i].mapping_refs=0;
        objects[i].operation_refs=0;
        for (uint32_t j=0; j<ZEROOS_SHMEM_MAX_PAGES; ++j)
            objects[i].pages[j]=0;
    }
    for (uint32_t i=0; i<ZEROOS_SHMEM_MAX_CAPABILITIES; ++i) {
        capabilities[i].generation=0;
        capabilities[i].used=0;
        capabilities[i].rights=0;
        capabilities[i].owner=0;
        capabilities[i].object=0;
    }
    for (uint32_t i=0; i<ZEROOS_SHMEM_MAX_MAPPINGS; ++i) {
        mappings[i].used=0;
        mappings[i].owner=0;
        mappings[i].object=0;
        mappings[i].virtual_address=0;
        mappings[i].page_count=0;
    }
    return 0;
}

int shmem_create(struct process *owner, uint64_t size, uint64_t flags,
                 zeroos_shmem_handle_t *handle_out) {
    uint32_t page_count;
    struct shmem_object *object=0;
    uint64_t irq_flags;
    int result=-ZEROOS_ENOMEM;

    if (!process_can_use(owner) || !handle_out || flags!=0 ||
        object_page_count_for_size(size,&page_count)!=0)
        return -ZEROOS_EINVAL;
    *handle_out=0;
    irq_flags=spin_lock_irqsave(&shmem_lock);
    for (uint32_t i=0; i<ZEROOS_SHMEM_MAX_OBJECTS; ++i) {
        if (!objects[i].used && objects[i].generation!=0xffffffffU) {
            object=&objects[i];
            break;
        }
    }
    if (!object)
        goto out;
    ++object->generation;
    object->used=1;
    object->page_count=page_count;
    object->cap_refs=0;
    object->mapping_refs=0;
    object->operation_refs=0;
    for (uint32_t i=0; i<page_count; ++i) {
        void *page=page_alloc_zero();
        if (!page)
            goto allocation_fail;
        object->pages[i]=(uint64_t)page;
    }
    result=capability_alloc_locked(owner,object,ZEROOS_SHMEM_ALL_RIGHTS,
                                   handle_out);
    if (result!=0)
        goto allocation_fail;
    goto out;

allocation_fail:
    for (uint32_t i=0; i<object->page_count; ++i) {
        if (object->pages[i])
            page_free((void *)(uint64_t)object->pages[i]);
        object->pages[i]=0;
    }
    object->used=0;
    object->page_count=0;
    object->cap_refs=0;
    object->mapping_refs=0;
    object->operation_refs=0;
out:
    spin_unlock_irqrestore(&shmem_lock,irq_flags);
    return result;
}

int shmem_grant(struct process *owner, zeroos_shmem_handle_t source,
                uint64_t target_pid, uint8_t rights,
                zeroos_shmem_handle_t *target_out) {
    struct process *target;
    uint64_t irq_flags;
    int result;

    if (!process_can_use(owner) || !target_out || target_pid==0 ||
        !rights || (rights&~ZEROOS_SHMEM_ALL_RIGHTS)!=0)
        return -ZEROOS_EINVAL;
    if (process_acquire_live(target_pid,&target)!=0)
        return -ZEROOS_ENOENT;
    *target_out=0;
    /* As with IPC grants, pin the process before taking the subsystem lock;
     * process teardown takes the process lock first and then revokes caps. */
    irq_flags=spin_lock_irqsave(&shmem_lock);
    {
        struct shmem_capability *capability=
            capability_lookup_locked(owner,source);
        if (!capability || !(capability->rights&ZEROOS_SHMEM_RIGHT_GRANT))
            result=-ZEROOS_EBADF;
        else {
            rights&=capability->rights;
            if (!rights)
                result=-ZEROOS_EPERM;
            else
                result=capability_alloc_locked(target,capability->object,
                                               rights,target_out);
        }
    }
    spin_unlock_irqrestore(&shmem_lock,irq_flags);
    (void)process_release_live(target);
    return result;
}

int shmem_map(struct process *owner, zeroos_shmem_handle_t handle,
              uint64_t virtual_address, uint64_t flags,
              uint64_t *mapped_out) {
    uint64_t pages[ZEROOS_SHMEM_MAX_PAGES];
    uint32_t page_count;
    uint32_t mapped_pages=0;
    struct shmem_object *object;
    struct shmem_mapping *mapping=0;
    uint64_t irq_flags;
    uint64_t length;
    int result=-ZEROOS_EBUSY;

    if (!process_can_use(owner) || !mapped_out ||
        (flags&~ZEROOS_SHMEM_VALID_MAP_FLAGS)!=0 ||
        !virtual_address || (virtual_address&(VMM_PAGE_SIZE-1ULL)))
        return -ZEROOS_EINVAL;
    irq_flags=spin_lock_irqsave(&shmem_lock);
    {
        struct shmem_capability *capability=
            capability_lookup_locked(owner,handle);
        if (!capability || !(capability->rights&ZEROOS_SHMEM_RIGHT_MAP)) {
            spin_unlock_irqrestore(&shmem_lock,irq_flags);
            return -ZEROOS_EBADF;
        }
        if ((flags&ZEROOS_SHMEM_MAP_WRITE) &&
            !(capability->rights&ZEROOS_SHMEM_RIGHT_WRITE)) {
            spin_unlock_irqrestore(&shmem_lock,irq_flags);
            return -ZEROOS_EPERM;
        }
        object=capability->object;
        page_count=object->page_count;
        length=(uint64_t)page_count*VMM_PAGE_SIZE;
        if (virtual_address>~0ULL-length ||
            ((virtual_address+length-1ULL)>>39)!=
                ((ZEROOS_USER_BASE)>>39)) {
            spin_unlock_irqrestore(&shmem_lock,irq_flags);
            return -ZEROOS_EINVAL;
        }
        for (uint32_t i=0; i<ZEROOS_SHMEM_MAX_MAPPINGS; ++i)
            if (!mappings[i].used) {
                mapping=&mappings[i];
                break;
            }
        if (!mapping) {
            spin_unlock_irqrestore(&shmem_lock,irq_flags);
            return -ZEROOS_ENOMEM;
        }
        mapping->used=1;
        mapping->owner=owner;
        mapping->object=object;
        mapping->virtual_address=virtual_address;
        mapping->page_count=page_count;
        ++object->mapping_refs;
        for (uint32_t i=0; i<page_count; ++i)
            pages[i]=object->pages[i];
        ++object->operation_refs;
    }
    spin_unlock_irqrestore(&shmem_lock,irq_flags);

    for (uint32_t i=0; i<page_count; ++i) {
        uint64_t map_flags=VMM_USER|VMM_NO_EXECUTE;
        if (flags&ZEROOS_SHMEM_MAP_WRITE)
            map_flags|=VMM_WRITABLE;
        if (process_address_space_map_page(owner,
                                           virtual_address+i*VMM_PAGE_SIZE,
                                           pages[i],map_flags)!=0)
            goto rollback;
        ++mapped_pages;
    }
    *mapped_out=virtual_address;
    result=0;
    goto operation_done;

rollback:
    if (mapped_pages)
        (void)process_address_space_unmap_range(owner,virtual_address,
                                                pages,mapped_pages);

operation_done:
    irq_flags=spin_lock_irqsave(&shmem_lock);
    if (result!=0 && mapping && mapping->used && mapping->object==object) {
        mapping->used=0;
        mapping->owner=0;
        mapping->object=0;
        mapping->virtual_address=0;
        mapping->page_count=0;
        if (object->mapping_refs)
            --object->mapping_refs;
    }
    if (object->used && object->operation_refs)
        --object->operation_refs;
    object_destroy_locked(object);
    spin_unlock_irqrestore(&shmem_lock,irq_flags);
    return result;
}

int shmem_unmap(struct process *owner, zeroos_shmem_handle_t handle,
                uint64_t virtual_address) {
    uint64_t pages[ZEROOS_SHMEM_MAX_PAGES];
    uint32_t page_count;
    struct shmem_object *object;
    struct shmem_mapping *mapping=0;
    uint64_t irq_flags;
    int result=0;

    if (!process_can_use(owner) || !virtual_address ||
        (virtual_address&(VMM_PAGE_SIZE-1ULL)))
        return -ZEROOS_EINVAL;
    irq_flags=spin_lock_irqsave(&shmem_lock);
    {
        struct shmem_capability *capability=
            capability_lookup_locked(owner,handle);
        if (!capability || !(capability->rights&ZEROOS_SHMEM_RIGHT_MAP)) {
            spin_unlock_irqrestore(&shmem_lock,irq_flags);
            return -ZEROOS_EBADF;
        }
        object=capability->object;
        page_count=object->page_count;
        if (virtual_address>~0ULL-
                (uint64_t)page_count*VMM_PAGE_SIZE ||
            ((virtual_address+((uint64_t)page_count*VMM_PAGE_SIZE)-1ULL)>>39)!=
                ((ZEROOS_USER_BASE)>>39)) {
            spin_unlock_irqrestore(&shmem_lock,irq_flags);
            return -ZEROOS_EINVAL;
        }
        for (uint32_t i=0; i<ZEROOS_SHMEM_MAX_MAPPINGS; ++i)
            if (mappings[i].used && mappings[i].owner==owner &&
                mappings[i].object==object &&
                mappings[i].virtual_address==virtual_address) {
                mapping=&mappings[i];
                break;
            }
        if (!mapping) {
            spin_unlock_irqrestore(&shmem_lock,irq_flags);
            return -ZEROOS_EINVAL;
        }
        for (uint32_t i=0; i<page_count; ++i)
            pages[i]=object->pages[i];
        ++object->operation_refs;
    }
    spin_unlock_irqrestore(&shmem_lock,irq_flags);

    if (process_address_space_unmap_range(owner,virtual_address,pages,
                                          page_count)!=0)
        result=-ZEROOS_EBUSY;

    irq_flags=spin_lock_irqsave(&shmem_lock);
    if (result==0 && mapping && mapping->used && mapping->object==object) {
        mapping->used=0;
        mapping->owner=0;
        mapping->object=0;
        mapping->virtual_address=0;
        mapping->page_count=0;
        if (object->mapping_refs)
            --object->mapping_refs;
    }
    if (object->used && object->operation_refs)
        --object->operation_refs;
    object_destroy_locked(object);
    spin_unlock_irqrestore(&shmem_lock,irq_flags);
    return result;
}

int shmem_close(struct process *owner, zeroos_shmem_handle_t handle) {
    uint64_t irq_flags;
    if (!process_can_use(owner))
        return -ZEROOS_EPERM;
    irq_flags=spin_lock_irqsave(&shmem_lock);
    {
        struct shmem_capability *capability=
            capability_lookup_locked(owner,handle);
        if (!capability || !(capability->rights&ZEROOS_SHMEM_RIGHT_CLOSE)) {
            spin_unlock_irqrestore(&shmem_lock,irq_flags);
            return -ZEROOS_EBADF;
        }
        for (uint32_t i=0; i<ZEROOS_SHMEM_MAX_MAPPINGS; ++i)
            if (mappings[i].used && mappings[i].owner==owner &&
                mappings[i].object==capability->object) {
                spin_unlock_irqrestore(&shmem_lock,irq_flags);
                return -ZEROOS_EBUSY;
            }
        capability_drop_locked(capability);
    }
    spin_unlock_irqrestore(&shmem_lock,irq_flags);
    return 0;
}

int shmem_process_revoke(struct process *owner) {
    uint64_t irq_flags;
    if (!owner)
        return -ZEROOS_EINVAL;
    irq_flags=spin_lock_irqsave(&shmem_lock);
    /* Process teardown owns the process lock at this call site. Unmap the
     * tracked shared mappings directly so vmm_space_destroy sees an exact
     * resident-page count, then revoke the handles and release object pages. */
    for (uint32_t i=0; i<ZEROOS_SHMEM_MAX_MAPPINGS; ++i) {
        struct shmem_mapping *mapping=&mappings[i];
        if (!mapping->used || mapping->owner!=owner)
            continue;
        if (!mapping->object || !mapping->object->used ||
            mapping->object->operation_refs) {
            spin_unlock_irqrestore(&shmem_lock,irq_flags);
            return -ZEROOS_EBUSY;
        }
        for (uint32_t page=0; page<mapping->page_count; ++page) {
            uint64_t address=mapping->virtual_address+
                              page*VMM_PAGE_SIZE;
            if (vmm_space_translate(&owner->address_space,address)!=
                    mapping->object->pages[page] ||
                vmm_space_unmap_page(&owner->address_space,address)!=0 ||
                owner->resident_pages==0) {
                spin_unlock_irqrestore(&shmem_lock,irq_flags);
                return -ZEROOS_EBUSY;
            }
            --owner->resident_pages;
        }
        if (mapping->object->mapping_refs)
            --mapping->object->mapping_refs;
        mapping->used=0;
        mapping->owner=0;
        mapping->object=0;
        mapping->virtual_address=0;
        mapping->page_count=0;
    }
    for (uint32_t i=0; i<ZEROOS_SHMEM_MAX_CAPABILITIES; ++i)
        if (capabilities[i].used && capabilities[i].owner==owner)
            capability_drop_locked(&capabilities[i]);
    for (uint32_t i=0; i<ZEROOS_SHMEM_MAX_OBJECTS; ++i)
        object_destroy_locked(&objects[i]);
    spin_unlock_irqrestore(&shmem_lock,irq_flags);
    return 0;
}

int shmem_debug_validate(void) {
    uint64_t irq_flags=spin_lock_irqsave(&shmem_lock);
    for (uint32_t i=0; i<ZEROOS_SHMEM_MAX_OBJECTS; ++i) {
        struct shmem_object *object=&objects[i];
        uint32_t refs=0;
        if (!object->used) {
            if (object->page_count || object->cap_refs ||
                object->mapping_refs || object->operation_refs) {
                spin_unlock_irqrestore(&shmem_lock,irq_flags);
                return -1;
            }
            continue;
        }
        if (!object->page_count || object->page_count>ZEROOS_SHMEM_MAX_PAGES ||
            (object->cap_refs==0 && object->mapping_refs==0)) {
            spin_unlock_irqrestore(&shmem_lock,irq_flags);
            return -1;
        }
        for (uint32_t j=0; j<object->page_count; ++j)
            if (!object->pages[j] || !memory_page_is_allocated(object->pages[j])) {
                spin_unlock_irqrestore(&shmem_lock,irq_flags);
                return -1;
            }
        for (uint32_t j=0; j<ZEROOS_SHMEM_MAX_CAPABILITIES; ++j)
            if (capabilities[j].used && capabilities[j].object==object)
                ++refs;
        if (refs!=object->cap_refs) {
            spin_unlock_irqrestore(&shmem_lock,irq_flags);
            return -1;
        }
        refs=0;
        for (uint32_t j=0; j<ZEROOS_SHMEM_MAX_MAPPINGS; ++j)
            if (mappings[j].used && mappings[j].object==object)
                ++refs;
        if (refs!=object->mapping_refs) {
            spin_unlock_irqrestore(&shmem_lock,irq_flags);
            return -1;
        }
    }
    for (uint32_t i=0; i<ZEROOS_SHMEM_MAX_MAPPINGS; ++i) {
        if (mappings[i].used && (!mappings[i].owner ||
            !process_can_use(mappings[i].owner) || !mappings[i].object ||
            !mappings[i].object->used || mappings[i].page_count==0 ||
            mappings[i].page_count>ZEROOS_SHMEM_MAX_PAGES ||
            !mappings[i].virtual_address ||
            (mappings[i].virtual_address&(VMM_PAGE_SIZE-1ULL)))) {
            spin_unlock_irqrestore(&shmem_lock,irq_flags);
            return -1;
        }
    }
    for (uint32_t i=0; i<ZEROOS_SHMEM_MAX_CAPABILITIES; ++i) {
        struct shmem_capability *capability=&capabilities[i];
        if (!capability->used)
            continue;
        if (!process_can_use(capability->owner) || !capability->object ||
            !capability->object->used || !capability->rights ||
            (capability->rights&~ZEROOS_SHMEM_ALL_RIGHTS)!=0) {
            spin_unlock_irqrestore(&shmem_lock,irq_flags);
            return -1;
        }
    }
    spin_unlock_irqrestore(&shmem_lock,irq_flags);
    return 0;
}

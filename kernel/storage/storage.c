/*
 * Storage manager: owns bring-up ordering of the Stage 3 stack and the
 * boot-time certification sequence. Runs as an ordinary kernel task so all
 * storage waits are real blocking waits (no busy polling in the monitor).
 */
#include "storage.h"
#include "block.h"
#include "ahci.h"
#include "gpt.h"
#include "nvme.h"
#include "pagecache.h"
#include "vfs.h"
#include "zjfs.h"
#include "../crc.h"
#include "../kstring.h"
#include "../memory.h"
#include "../ksync.h"
#include "fsyscall.h"
#include "pagecache.h"
#include "../elf.h"
#include "../process.h"
#include "../thread.h"
#include "../user.h"
#include "../vmm.h"
#include "../pci.h"
#include "../task.h"
#include "../timer.h"

static uint64_t storage_task_id;
static volatile int storage_done;
static volatile int storage_started;

static void storage_fail(const char *what) {
    klog("ZEROOS PANIC: storage certification failed: %s.",what);
    for (;;)
        __asm__ volatile ("cli; hlt");
}

static struct kcompletion storage_event;
static void storage_persistence_check(void);
static const struct vfs_cred storage_root_cred={0,0};

static void storage_worker_kick(void) {
    kcompletion_signal(&storage_event);
}

/* Mount policy: only partitions whose GPT type is ZEROOS-ZJFS and whose
 * superblock validates are mounted; the first at /data, others at
 * /diskN. Nothing on persistent media is ever formatted implicitly. */
static void storage_mount_policy(void) {
    uint32_t mounted=0;
    uint32_t count=block_device_count();
    for (uint32_t i=0; i<count; ++i) {
        struct block_device *part=block_device_at(i);
        if (!part || !part->parent)
            continue;
        struct block_device *disk=part->parent;
        if (!ahci_is_ahci_device(disk) && !nvme_is_nvme_device(disk))
            continue;
        if (!gpt_guid_equal(part->type_guid,gpt_type_zeroos_zjfs))
            continue;
        if (zjfs_probe(part)!=0) {
            klog("ZEROOS: storage: %s has the ZJFS partition type but no valid filesystem; not mounted (never auto-formatted).",
                 part->name);
            continue;
        }
        char path[16];
        if (mounted==0)
            ksnprintf(path,sizeof(path),"/data");
        else
            ksnprintf(path,sizeof(path),"/disk%u",mounted);
        if (vfs_mount(part,path,0)==0)
            ++mounted;
    }
    if (mounted)
        storage_persistence_check();
    else
        klog("ZEROOS: storage: no ZJFS partition found; persistent mount skipped.");
}

static uint64_t storage_parse_u64(const char *text, uint64_t length) {
    uint64_t value=0;
    for (uint64_t i=0; i<length && text[i]>='0' && text[i]<='9'; ++i)
        value=value*10U+(uint64_t)(text[i]-'0');
    return value;
}

/* Boot counter on /data (proves persistence across boots, including
 * unclean ones), host-provided file check, clean unmount/remount. */
static void storage_persistence_check(void) {
    const struct vfs_cred *cred=&storage_root_cred;
    struct vfs_file *f;
    char text[32];
    uint64_t previous=0;
    if (vfs_open(cred,"/data/.zeroos-boot-count",VFS_O_RDONLY,0,&f)==0) {
        int64_t n=vfs_read(f,text,sizeof(text)-1U);
        if (n>0)
            previous=storage_parse_u64(text,(uint64_t)n);
        vfs_file_put(f);
    }
    uint64_t length=ksnprintf(text,sizeof(text),"%llu\n",previous+1U);
    int rc=vfs_open(cred,"/data/.zeroos-boot-count",VFS_O_CREAT|VFS_O_TRUNC|VFS_O_WRONLY,
                    0644,&f);
    if (rc==0) {
        int64_t n=vfs_write(f,text,length);
        rc=n==(int64_t)length ? vfs_fsync(f,0) : (n<0 ? (int)n : -SE_IO);
        vfs_file_put(f);
    }
    if (rc) {
        klog("ZEROOS: storage persistence: boot counter update failed (%d).",rc);
        storage_fail("persistent boot counter");
    }
    klog("ZEROOS: storage persistence: /data boot counter %llu (previous %llu).",
         previous+1U,previous);
    if (vfs_open(cred,"/data/hello.txt",VFS_O_RDONLY,0,&f)==0) {
        uint8_t *page=(uint8_t *)page_alloc();
        uint64_t total=0;
        uint32_t crc=0;
        int64_t n;
        while (page && (n=vfs_read(f,page,4096))>0) {
            crc=crc32c_update(crc,page,(uint64_t)n);
            total+=(uint64_t)n;
        }
        if (page)
            page_free(page);
        vfs_file_put(f);
        klog("ZEROOS: storage persistence: host file /data/hello.txt bytes=%llu crc32c=0x%x.",
             total,crc);
    }
    struct vfs_statfs before;
    (void)vfs_statfs("/data",&before);
    struct block_device *device=0;
    uint32_t count=block_device_count();
    for (uint32_t i=0; i<count && !device; ++i) {
        struct block_device *part=block_device_at(i);
        if (part && part->parent && gpt_guid_equal(part->type_guid,gpt_type_zeroos_zjfs) &&
            part->refcount)
            device=part;
    }
    rc=vfs_unmount("/data",0);
    uint8_t *page=(uint8_t *)page_alloc();
    int clean=0;
    if (rc==0 && device && page &&
        block_rw(device,BLOCK_OP_READ,0,page,4096,BLOCK_PRIO_NORMAL)==0)
        clean=((struct zj_super *)page)->state==ZJ_STATE_CLEAN;
    if (page)
        page_free(page);
    if (rc==0 && device)
        rc=vfs_mount(device,"/data",0);
    if (rc || !clean) {
        klog("ZEROOS: storage persistence: clean unmount/remount failed (rc=%d clean=%d).",rc,clean);
        storage_fail("clean unmount/remount");
    }
    klog("ZEROOS: storage persistence check passed (clean unmount -> CLEAN superblock -> remount).");
}

/* Volatile scratch filesystem for userspace on machines without disks.
 * The kernel created this RAM device itself, so formatting it is not
 * "auto-formatting user media". */
static void storage_mount_ram(void) {
    struct block_device *ram=block_find("ramfs0");
    struct zj_format_options options;
    memset(&options,0,sizeof(options));
    options.label="ram";
    if (!ram || zjfs_format(ram,&options)!=0 || vfs_mount(ram,"/ram",0)!=0)
        storage_fail("volatile /ram filesystem");
}

/* Storage worker: event-driven. Wakes on fd-table teardown or when a
 * journal transaction opens; while one is open it wakes every 100 ticks
 * to apply the commit interval; otherwise it sleeps indefinitely. */
static void storage_worker(void *argument) {
    (void)argument;
    for (;;) {
        kcompletion_reset(&storage_event);
        vfs_deferred_work();
        fsyscall_deferred_work();
        if (vfs_periodic())
            task_sleep_ticks(100);
        else
            kcompletion_wait(&storage_event);
    }
}

/* Post-certification: a write without fsync must be made durable by the
 * worker's periodic commit (ZJ_COMMIT_INTERVAL) with no other trigger. */
static void storage_worker_probe(void) {
    struct vfs_file *f;
    if (vfs_open(&storage_root_cred,"/ram/.worker-probe",VFS_O_CREAT|VFS_O_WRONLY|VFS_O_TRUNC,
                 0644,&f)!=0)
        storage_fail("worker probe open");
    struct zj_metrics before, after;
    struct vfs_superblock *sb=f->inode->sb;     /* pinned by the open file */
    zjfs_metrics_snapshot(sb,&before);
    uint64_t t0=timer_ticks();
    (void)vfs_write(f,"probe\n",6);
    uint64_t waited=0;
    do {
        task_sleep_ticks(20);
        waited=timer_ticks()-t0;
        zjfs_metrics_snapshot(sb,&after);
    } while (after.commits==before.commits && waited<ZJ_COMMIT_INTERVAL*2U);
    vfs_file_put(f);
    if (after.commits==before.commits)
        storage_fail("storage worker periodic commit");
    klog("ZEROOS: storage worker periodic commit verified (%llu ticks after write, interval %u).",
         waited,ZJ_COMMIT_INTERVAL);
}

extern const uint8_t storage_probe_image_start[];
extern const uint8_t storage_probe_image_end[];

/* Runs the embedded Ring-3 storage probe as a fresh uid-0 process, reaps
 * it, then verifies the storage worker released the descriptor table and
 * the mmap pins the probe deliberately left behind at exit. */
static void storage_run_user_probe(void) {
    uint64_t image_size=(uint64_t)(storage_probe_image_end-storage_probe_image_start);
    struct zeroos_elf_load_result load={0};
    process_id_t pid;
    thread_id_t tid;
    if (process_create(0,&pid)!=0)
        storage_fail("user probe process");
    struct process *process=process_lookup(pid);
    uint32_t generation=process ? process->generation : 0;
    void *stack=page_alloc_zero();
    if (!process || process_set_limits(process,1,1,512)!=0 ||
        elf_load_image(process,storage_probe_image_start,image_size,&load)!=0 || !stack ||
        process_address_space_map_page(process,ZEROOS_USER_STACK_PAGE,(uint64_t)stack,
                                       VMM_USER|VMM_WRITABLE|VMM_NO_EXECUTE)!=0)
        storage_fail("user probe image load");
    page_free(stack);                           /* the mapping holds the frame */
    if (thread_create_user(process,load.entry,ZEROOS_USER_STACK_TOP,&tid)!=0)
        storage_fail("user probe thread");
    struct thread *thread=thread_lookup(tid);
    uint64_t t0=timer_ticks();
    while (thread && thread->state!=THREAD_ZOMBIE && timer_ticks()-t0<3000U)
        task_sleep_ticks(2);
    uint64_t status=~0ULL;
    if (!thread || thread->state!=THREAD_ZOMBIE)
        storage_fail("user probe did not finish");
    if (thread_reap(thread,&status)!=0)
        storage_fail("user probe thread reap");
    t0=timer_ticks();
    while (process->state!=PROCESS_ZOMBIE && timer_ticks()-t0<200U)
        task_sleep_ticks(1);
    if (vmm_activate_kernel()!=0 || process_reap(process,&status)!=0)
        storage_fail("user probe process reap");
    if (status!=0) {
        klog("ZEROOS: storage userspace probe exit status %llu.",status);
        storage_fail("user probe exit status");
    }
    /* Teardown: fd table + mapping released by the worker. */
    struct pc_stats pcs;
    t0=timer_ticks();
    do {
        task_sleep_ticks(2);
        pc_stats_snapshot(&pcs);
    } while ((vfs_fdtable_lookup(pid,generation,0) || pcs.pinned) && timer_ticks()-t0<500U);
    if (vfs_fdtable_lookup(pid,generation,0) || pcs.pinned) {
        klog("ZEROOS: storage userspace teardown incomplete (pinned=%llu).",pcs.pinned);
        storage_fail("user probe teardown");
    }
    klog("ZEROOS: storage userspace probe reaped; descriptors and mappings released (pinned=0, fdtables=%u).",
         vfs_fdtable_count());
}

static void storage_main(void *argument) {
    (void)argument;
    uint64_t start=timer_ticks();
    kcompletion_init(&storage_event);
    klog("ZEROOS: storage manager started.");
    if (crc_self_test()!=0)
        storage_fail("CRC32/CRC32C self-test");
    klog("ZEROOS: storage CRC32/CRC32C self-test passed.");
    block_init();
    block_watchdog_start();
    if (storage_selftest_block()!=0)
        storage_fail("block layer self-test");
    klog("ZEROOS: storage block layer scheduler/fault/timeout self-test passed.");
    if (storage_selftest_gpt()!=0)
        storage_fail("GPT validation self-test");
    klog("ZEROOS: storage GPT validation/recovery self-test passed.");
    pc_init();
    vfs_init();
    pc_flusher_start();
    if (storage_selftest_fs()!=0)
        storage_fail("VFS/ZJFS/page-cache self-test");
    klog("ZEROOS: storage VFS/ZJFS/page-cache self-test passed.");
    if (storage_selftest_crash()!=0)
        storage_fail("crash-consistency self-test");
    klog("ZEROOS: storage crash-consistency (power-cut/replay/fsck) self-test passed.");
    pci_init();
    int sata=ahci_probe_all();
    int nvme=nvme_probe_all();
    klog("ZEROOS: storage discovery: sata_disks=%d nvme_namespaces=%d.",sata,nvme);
    uint32_t devices=block_device_count();
    for (uint32_t i=0; i<devices; ++i) {
        struct block_device *disk=block_device_at(i);
        if (!disk || disk->parent || (!ahci_is_ahci_device(disk) &&
                                      !nvme_is_nvme_device(disk)))
            continue;
        static struct gpt_result table;
        if (gpt_scan(disk,&table)==0)
            gpt_register(disk,&table);
    }
    if (sata==0 && nvme==0)
        klog("ZEROOS: storage: no storage devices present (ramdisk-only certification).");
    for (uint32_t i=0; i<devices; ++i) {
        struct block_device *disk=block_device_at(i);
        if (!disk || disk->parent || (!ahci_is_ahci_device(disk) &&
                                      !nvme_is_nvme_device(disk)))
            continue;
        if (storage_selftest_driver(disk)!=0)
            storage_fail("hardware driver self-test");
    }
    storage_mount_policy();
    storage_mount_ram();
    vfs_set_worker_event(&storage_event);
    zjfs_set_txn_hook(storage_worker_kick);
    uint64_t worker_id;
    if (task_create(storage_worker,0,&worker_id)!=0)
        storage_fail("storage worker task creation");
    klog("ZEROOS: storage stack certification passed (ticks=%llu).",
         timer_ticks()-start);
    storage_done=1;
    storage_worker_probe();
    storage_run_user_probe();
    klog("ZEROOS: storage Stage 3 certification complete.");
    task_exit();
}

void storage_start(void) {
    if (storage_started)
        return;
    storage_started=1;
    if (task_create(storage_main,0,&storage_task_id)!=0)
        storage_fail("storage manager task creation");
}

int storage_finished(void) {
    return storage_done;
}

void storage_service_step(void) {
}

#include "power.h"
#include "acpi.h"
#include "kstring.h"
#include "task.h"
#include "timer.h"
#include "storage/errno.h"
#include "storage/storage.h"

#define PM1_SCI_EN (1U << 0)
#define PM1_SLP_TYP_SHIFT 10U
#define PM1_SLP_TYP_MASK (7U << PM1_SLP_TYP_SHIFT)
#define PM1_SLP_EN (1U << 13)
#define ACPI_ENABLE_WAIT_TICKS 300U     /* 3 s at 100 Hz */
#define TRANSITION_WAIT_TICKS 500U      /* 5 s for S5/reset to take effect */
#define CMDLINE_MAX 256U

static volatile uint32_t shutdown_started;
static uint8_t auto_poweroff;
static uint8_t auto_triggered;
/* S5 values come from the AML parser; trust them only once its boot
 * self-test has passed (power_report_capability). */
static volatile uint32_t s5_parser_verified;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

int power_check(uint32_t action) {
    if (action==POWER_ACTION_REBOOT)
        return 0;                           /* reset has a CPU-level fallback */
    if (action!=POWER_ACTION_POWEROFF)
        return -SE_INVAL;
    if (!__atomic_load_n(&s5_parser_verified,__ATOMIC_ACQUIRE))
        return -SE_NOTSUP;
    return acpi_info()->s5_supported ? 0 : -SE_NOTSUP;
}

int power_shutdown_in_progress(void) {
    return __atomic_load_n(&shutdown_started,__ATOMIC_ACQUIRE)!=0;
}

static void wait_ticks(uint64_t ticks) {
    uint64_t start=timer_ticks();
    while (timer_ticks()-start<ticks)
        (void)task_sleep_ticks(1);
}

/* SCI_EN must be set before SLP_EN is honoured on legacy (SMM) firmware. */
static void acpi_enable_mode(const struct acpi_info *acpi) {
    if (inw(acpi->pm1a_control_port)&PM1_SCI_EN)
        return;
    if (!acpi->smi_command_port || !acpi->acpi_enable_value) {
        klog("ZEROOS: shutdown: SCI_EN clear and no SMI handoff; attempting S5 anyway.");
        return;
    }
    outb(acpi->smi_command_port,acpi->acpi_enable_value);
    uint64_t start=timer_ticks();
    while (!(inw(acpi->pm1a_control_port)&PM1_SCI_EN) &&
           timer_ticks()-start<ACPI_ENABLE_WAIT_TICKS)
        (void)task_sleep_ticks(1);
    if (!(inw(acpi->pm1a_control_port)&PM1_SCI_EN))
        klog("ZEROOS: shutdown: ACPI mode enable timed out; attempting S5 anyway.");
}

static void acpi_enter_s5(void) {
    const struct acpi_info *acpi=acpi_info();
    acpi_enable_mode(acpi);
    uint16_t a=(uint16_t)((inw(acpi->pm1a_control_port)&~PM1_SLP_TYP_MASK)|
                          (acpi->slp_typ_a<<PM1_SLP_TYP_SHIFT));
    uint16_t b=0;
    if (acpi->pm1b_control_port)
        b=(uint16_t)((inw(acpi->pm1b_control_port)&~PM1_SLP_TYP_MASK)|
                     (acpi->slp_typ_b<<PM1_SLP_TYP_SHIFT));
    /* Program SLP_TYP first, then set SLP_EN (ACPI 6.5 §16.1.7). */
    outw(acpi->pm1a_control_port,a);
    if (acpi->pm1b_control_port)
        outw(acpi->pm1b_control_port,b);
    outw(acpi->pm1a_control_port,(uint16_t)(a|PM1_SLP_EN));
    if (acpi->pm1b_control_port)
        outw(acpi->pm1b_control_port,(uint16_t)(b|PM1_SLP_EN));
}

static void platform_reset(void) {
    const struct acpi_info *acpi=acpi_info();
    if (acpi->reset_supported) {
        outb(acpi->reset_port,acpi->reset_value);
        wait_ticks(10);
    }
    /* i8042 CPU reset pulse. */
    for (uint32_t i=0; i<100000U && (inb(0x64)&2U); ++i)
        __asm__ volatile ("pause");
    outb(0x64,0xFE);
    wait_ticks(10);
    /* Last resort: triple fault through an empty IDT. */
    struct __attribute__((packed)) { uint16_t limit; uint64_t base; } idt={0,0};
    __asm__ volatile ("cli; lidt %0; int3" : : "m"(idt) : "memory");
}

int power_request(uint32_t action, const char *reason) {
    int rc=power_check(action);
    if (rc)
        return rc;
    uint32_t expected=0;
    if (!__atomic_compare_exchange_n(&shutdown_started,&expected,1U,0,
                                     __ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))
        return -SE_BUSY;
    const char *what=action==POWER_ACTION_POWEROFF ? "power-off" : "reboot";
    klog("ZEROOS: shutdown: %s requested (reason=%s).",what,reason ? reason : "unspecified");

    struct storage_shutdown_report report;
    storage_shutdown(&report);
    klog("ZEROOS: shutdown: filesystems synced (result %d) and unmounted (result %d, mounts=%u).",
         report.sync_result,report.unmount_result,report.mounts);
    klog("ZEROOS: shutdown: device write caches flushed (disks=%u, failed=%u).",
         report.disks_flushed+report.flush_failures,report.flush_failures);
    klog("ZEROOS: shutdown: NVMe controllers shut down (timeouts=%d).",report.nvme_timeouts);
    if (report.sync_result==0 && report.unmount_result==0 &&
        report.flush_failures==0 && report.nvme_timeouts==0)
        klog("ZEROOS: shutdown: storage quiesced cleanly.");
    else
        klog("ZEROOS: shutdown: storage quiesce reported errors; journal recovery will run at next mount.");

    if (action==POWER_ACTION_POWEROFF) {
        const struct acpi_info *acpi=acpi_info();
        klog("ZEROOS: shutdown: entering ACPI S5 (pm1a=0x%x slp_typ=%u).",
             (unsigned)acpi->pm1a_control_port,(unsigned)acpi->slp_typ_a);
        acpi_enter_s5();
        wait_ticks(TRANSITION_WAIT_TICKS);
        klog("ZEROOS: shutdown: ACPI S5 did not take effect; halting this CPU.");
    } else {
        klog("ZEROOS: shutdown: resetting platform.");
        platform_reset();
        klog("ZEROOS: shutdown: platform reset did not take effect; halting this CPU.");
    }
    for (;;)
        __asm__ volatile ("cli; hlt" ::: "memory");
}

/* ---------------------------------------------------- boot options/monitor */

#define MB2_TAG_END 0U
#define MB2_TAG_CMDLINE 1U
#define MB2_INFO_MAX (16U * 1024U * 1024U)

static int option_present(const char *line, uint32_t length, const char *option) {
    uint32_t n=0;
    while (option[n])
        ++n;
    for (uint32_t i=0; i+n<=length; ++i) {
        if (i>0 && line[i-1U]!=' ')
            continue;
        uint32_t k=0;
        while (k<n && line[i+k]==option[k])
            ++k;
        if (k==n && (i+n==length || line[i+n]==' ' || line[i+n]==0))
            return 1;
    }
    return 0;
}

void power_parse_boot_options(uint64_t multiboot_info) {
    if (!multiboot_info)
        return;
    uint32_t total=*(const uint32_t *)(uint64_t)multiboot_info;
    if (total<16U || total>MB2_INFO_MAX)
        return;
    uint32_t cursor=8U;
    while (cursor+8U<=total) {
        const uint8_t *tag=(const uint8_t *)(uint64_t)(multiboot_info+cursor);
        uint32_t type=*(const uint32_t *)tag;
        uint32_t size=*(const uint32_t *)(tag+4);
        if (type==MB2_TAG_END || size<8U || size>total-cursor)
            return;
        if (type==MB2_TAG_CMDLINE) {
            const char *line=(const char *)(tag+8);
            uint32_t length=0;
            while (length<size-8U && length<CMDLINE_MAX && line[length])
                ++length;
            if (option_present(line,length,"zeroos.shutdown=poweroff-after-cert")) {
                auto_poweroff=1;
                klog("ZEROOS: boot option: power off after certification.");
            }
            return;
        }
        cursor+=(size+7U)&~7U;
    }
}

void power_report_capability(void) {
    const struct acpi_info *acpi=acpi_info();
    int parser=acpi_s5_self_test();
    if (parser!=0) {
        klog("ZEROOS: ACPI S5 parser self-test FAILED (case %d); power-off disabled.",-parser);
        return;
    }
    __atomic_store_n(&s5_parser_verified,1U,__ATOMIC_RELEASE);
    if (acpi->s5_supported)
        klog("ZEROOS: ACPI power: S5 soft-off available (pm1a=0x%x pm1b=0x%x slp_typ=%u/%u), reset=%s.",
             (unsigned)acpi->pm1a_control_port,(unsigned)acpi->pm1b_control_port,
             (unsigned)acpi->slp_typ_a,(unsigned)acpi->slp_typ_b,
             acpi->reset_supported ? "ACPI reset register" : "i8042/triple-fault");
    else
        klog("ZEROOS: ACPI power: S5 soft-off unavailable (%s); reset=%s.",
             acpi_s5_error_name(acpi->s5_error),
             acpi->reset_supported ? "ACPI reset register" : "i8042/triple-fault");
}

static void power_auto_task(void *argument) {
    (void)argument;
    int rc=power_request(POWER_ACTION_POWEROFF,"boot option poweroff-after-cert");
    klog("ZEROOS: shutdown: automatic power-off refused (%d).",rc);
    task_exit();
}

void power_monitor_step(int certification_complete) {
    if (!auto_poweroff || auto_triggered || !certification_complete)
        return;
    auto_triggered=1;
    uint64_t id;
    if (task_create(power_auto_task,0,&id)!=0)
        klog("ZEROOS: shutdown: automatic power-off task creation failed.");
}

#include "session.h"
#include "types.h"
#include "kstring.h"
#include "memory.h"
#include "vmm.h"
#include "timer.h"
#include "task.h"
#include "process.h"
#include "thread.h"
#include "elf.h"
#include "user.h"
#include "syscall.h"

extern void serial_write_public(const char *text);

static volatile int session_started;
static volatile int session_done;
static uint64_t session_task_id;

extern const uint8_t session_probe_image_start[];
extern const uint8_t session_probe_image_end[];

/* Boot certification: a session failure is fatal, mirroring the storage
 * manager's certification-fail semantics (report, then halt). */
static void session_fail(const char *reason) {
    klog("ZEROOS PANIC: session certification failed: %s.", reason);
    session_done = 1;
    for (;;)
        __asm__ volatile ("cli; hlt");
}

/* Launch the embedded session ELF, wait for it, reap it. Mirrors the
 * storage probe lifecycle with boot-certification semantics. */
static void session_main(void *argument) {
    uint64_t image_size =
        (uint64_t)(session_probe_image_end - session_probe_image_start);
    struct zeroos_elf_load_result load = {0};
    process_id_t pid;
    thread_id_t tid;
    struct process *process;
    struct thread *thread;
    void *stack;
    uint64_t status = ~0ULL;
    uint64_t t0;

    (void)argument;
    serial_write_public("ZEROOS: session shell process started.\n");
    if (process_create(0, &pid) != 0)
        session_fail("process create");
    process = process_lookup(pid);
    if (!process || process_set_limits(process, 1, 1, 1024) != 0)
        session_fail("process limits");
    stack = page_alloc_zero();
    if (elf_load_image(process, session_probe_image_start, image_size,
                       &load) != 0 || !stack ||
        process_address_space_map_page(process, ZEROOS_USER_STACK_PAGE,
                                       (uint64_t)stack,
                                       VMM_USER | VMM_WRITABLE |
                                           VMM_NO_EXECUTE) != 0)
        session_fail("image load");
    page_free(stack); /* the mapping holds the frame */
    if (thread_create_user(process, load.entry, ZEROOS_USER_STACK_TOP,
                           &tid) != 0)
        session_fail("thread create");

    thread = thread_lookup(tid);
    t0 = timer_ticks();
    while (thread && thread->state != THREAD_ZOMBIE &&
           timer_ticks() - t0 < 3000U)
        task_sleep_ticks(2);
    if (!thread || thread->state != THREAD_ZOMBIE)
        session_fail("session did not finish");
    if (thread_reap(thread, &status) != 0)
        session_fail("thread reap");
    t0 = timer_ticks();
    while (process->state != PROCESS_ZOMBIE && timer_ticks() - t0 < 200U)
        task_sleep_ticks(1);
    if (vmm_activate_kernel() != 0 || process_reap(process, &status) != 0)
        session_fail("process reap");
    if (status != 0) {
        klog("ZEROOS: session exit status %llu.", status);
        session_fail("session exit status");
    }
    serial_write_public("ZEROOS: session shell process reaped cleanly.\n");
    session_done = 1;
    task_exit();
}

void session_start(void) {
    if (session_started)
        return;
    session_started = 1;
    if (task_create(session_main, 0, &session_task_id) != 0)
        session_fail("session task creation");
}

int session_finished(void) {
    return session_done;
}

#include <assert.h>
#include "../kernel/driver_core.h"

static int release_count = 0;
static int fail_release = 0;

static int test_release_fn(void *context, uint32_t resource) {
    (void)context;
    (void)resource;
    release_count++;
    return fail_release ? -1 : 0;
}

static void test_driver_lifecycle(void) {
    struct driver_instance d = {0};
    d.release = test_release_fn;
    release_count = 0;
    fail_release = 0;

    /* Full forward progression */
    assert(driver_transition(&d, DRIVER_DISCOVERED) == 0);
    assert(driver_transition(&d, DRIVER_MATCHED) == 0);
    assert(driver_transition(&d, DRIVER_PROBED) == 0);
    assert(driver_transition(&d, DRIVER_ACQUIRED) == 0);
    assert(driver_transition(&d, DRIVER_CONFIGURED) == 0);
    assert(driver_transition(&d, DRIVER_REGISTERED) == 0);
    assert(driver_transition(&d, DRIVER_SERVING) == 0);

    /* Suspend and resume */
    assert(driver_transition(&d, DRIVER_SUSPENDED) == 0);
    assert(driver_transition(&d, DRIVER_SERVING) == 0);

    /* Failure from serving */
    assert(driver_transition(&d, DRIVER_FAILED) == 0);

    /* Removal from failed */
    assert(driver_transition(&d, DRIVER_REMOVED) == 0);
}

static void test_driver_illegal_transitions(void) {
    struct driver_instance d = {0};

    /* NULL instance */
    assert(driver_transition(0, DRIVER_DISCOVERED) == -1);

    /* Skip steps from NEW */
    assert(driver_transition(&d, DRIVER_SERVING) == -1);
    assert(driver_transition(&d, DRIVER_MATCHED) == -1);
    assert(driver_transition(&d, DRIVER_PROBED) == -1);

    /* Removal transitions to terminal state */
    assert(driver_transition(&d, DRIVER_DISCOVERED) == 0);
    assert(driver_transition(&d, DRIVER_REMOVED) == 0);
    /* Cannot leave REMOVED */
    assert(driver_transition(&d, DRIVER_NEW) == -1);
    assert(driver_transition(&d, DRIVER_DISCOVERED) == -1);
    assert(driver_transition(&d, DRIVER_SERVING) == -1);
}

static void test_driver_resources(void) {
    struct driver_instance d = {0};
    d.release = test_release_fn;
    release_count = 0;
    fail_release = 0;

    assert(driver_transition(&d, DRIVER_DISCOVERED) == 0);
    assert(driver_transition(&d, DRIVER_MATCHED) == 0);
    assert(driver_transition(&d, DRIVER_PROBED) == 0);

    /* Out of range resource */
    assert(driver_resource_acquire(&d, DRIVER_MAX_RESOURCES) == -1);
    assert(driver_resource_release(&d, DRIVER_MAX_RESOURCES) == -1);

    /* Normal acquire */
    assert(driver_resource_acquire(&d, 0) == 0);
    assert(driver_resource_acquire(&d, 5) == 0);
    assert(driver_resource_acquire(&d, 31) == 0);

    /* Double acquire fails */
    assert(driver_resource_acquire(&d, 5) == -1);

    /* Release unacquired fails */
    assert(driver_resource_release(&d, 10) == -1);

    /* Normal release */
    assert(driver_resource_release(&d, 5) == 0);
    assert(release_count == 1);

    /* Re-acquire after release */
    assert(driver_resource_acquire(&d, 5) == 0);

    /* Cleanup releases all remaining (0, 5, 31) and transitions to REMOVED */
    assert(driver_cleanup(&d) == 0);
    assert(release_count == 4);
    assert(d.resources == 0);
    assert(d.state == DRIVER_REMOVED);

    /* Cannot acquire in REMOVED */
    assert(driver_resource_acquire(&d, 0) == -1);
}

static void test_driver_release_error(void) {
    struct driver_instance d = {0};
    d.release = test_release_fn;
    release_count = 0;
    fail_release = 1;

    assert(driver_transition(&d, DRIVER_DISCOVERED) == 0);
    assert(driver_resource_acquire(&d, 2) == 0);
    assert(driver_resource_release(&d, 2) == -2);
    /* Resource not dropped on callback failure */
    assert(d.resources == (1U << 2));

    fail_release = 0;
    assert(driver_cleanup(&d) == 0);
    assert(d.resources == 0);
    assert(d.state == DRIVER_REMOVED);
}

int main(void) {
    test_driver_lifecycle();
    test_driver_illegal_transitions();
    test_driver_resources();
    test_driver_release_error();
    return 0;
}

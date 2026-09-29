#include <assert.h>
#include <string.h>
#include "../kernel/input_core.h"

static void test_input_queue_basics(void) {
    struct input_queue q;
    struct input_event in = {0}, out = {0};

    input_queue_init(&q);
    assert(q.head == 0 && q.tail == 0 && q.dropped == 0);

    /* NULL checks */
    input_queue_init(0);
    assert(input_queue_push(0, &in) == -1);
    assert(input_queue_push(&q, 0) == -1);
    assert(input_queue_pop(0, &out) == -1);
    assert(input_queue_pop(&q, 0) == -1);

    /* Invalid kind */
    in.kind = 0;
    assert(input_queue_push(&q, &in) == -1);
    in.kind = INPUT_DEVICE_GONE + 1;
    assert(input_queue_push(&q, &in) == -1);

    /* Pop from empty queue returns 1 */
    assert(input_queue_pop(&q, &out) == 1);

    /* Push valid event */
    in.kind = INPUT_KEY;
    in.code = 30;
    in.value = 1;
    assert(input_queue_push(&q, &in) == 0);

    /* Pop returns 0 and event data */
    memset(&out, 0, sizeof(out));
    assert(input_queue_pop(&q, &out) == 0);
    assert(out.kind == INPUT_KEY && out.code == 30 && out.value == 1);

    /* Now queue is empty again */
    assert(input_queue_pop(&q, &out) == 1);
}

static void test_input_queue_capacity_and_drop(void) {
    struct input_queue q;
    struct input_event in = {0}, out = {0};

    input_queue_init(&q);
    in.kind = INPUT_POINTER;
    in.x = 10;
    in.y = 20;

    /* Fill queue: ring capacity is INPUT_QUEUE_CAPACITY (64), holds 63 */
    for (unsigned i = 0; i < INPUT_QUEUE_CAPACITY - 1; i++) {
        in.code = (uint16_t)i;
        assert(input_queue_push(&q, &in) == 0);
    }

    /* Next push fails with backpressure / drop */
    assert(input_queue_push(&q, &in) == -2);
    assert(q.dropped == 1);

    /* Pop one event */
    assert(input_queue_pop(&q, &out) == 0);
    assert(out.code == 0);

    /* Now one slot is available */
    in.code = 999;
    assert(input_queue_push(&q, &in) == 0);

    /* Queue is full again */
    assert(input_queue_push(&q, &in) == -2);
    assert(q.dropped == 2);
}

static void test_input_registry(void) {
    struct input_registry r;
    uint32_t ids[INPUT_MAX_DEVICES];
    uint32_t extra_id = 0;

    input_registry_init(&r);

    /* NULL / 0 checks */
    input_registry_init(0);
    assert(input_device_add(0, &extra_id) == -1);
    assert(input_device_add(&r, 0) == -1);
    assert(input_device_remove(0, 1) == -1);
    assert(input_device_remove(&r, 0) == -1);
    assert(input_device_remove(&r, 999) == -1);

    /* Register up to capacity */
    for (uint32_t i = 0; i < INPUT_MAX_DEVICES; i++) {
        assert(input_device_add(&r, &ids[i]) == 0);
        assert(ids[i] != 0);
        if (i > 0)
            assert(ids[i] != ids[i - 1]);
    }

    /* 33rd device fails */
    assert(input_device_add(&r, &extra_id) == -2);

    /* Remove device at slot 10 */
    assert(input_device_remove(&r, ids[10]) == 0);

    /* Second removal of same id fails */
    assert(input_device_remove(&r, ids[10]) == -1);

    /* Slot is now free for new registration */
    assert(input_device_add(&r, &extra_id) == 0);

    /* Remove remaining devices */
    assert(input_device_remove(&r, extra_id) == 0);
    for (uint32_t i = 0; i < INPUT_MAX_DEVICES; i++) {
        if (i != 10)
            assert(input_device_remove(&r, ids[i]) == 0);
    }
}

int main(void) {
    test_input_queue_basics();
    test_input_queue_capacity_and_drop();
    test_input_registry();
    return 0;
}

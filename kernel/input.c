#include "input.h"
#include "input_core.h"
#include "scancode_core.h"
#include "mouse_core.h"
#include "interrupts.h"
#include "apic.h"
#include "pic.h"
#include "wait.h"
#include "sync.h"
#include "timer.h"
#include "task.h"

extern void serial_write_public(const char *text);

/* i8042 ports and command bytes (PS/2 controller, universal PC). */
#define I8042_STATUS 0x64U
#define I8042_DATA   0x60U
#define I8042_STATUS_OBF (1U << 0)
#define I8042_STATUS_IBF (1U << 1)
#define I8042_STATUS_AUX (1U << 5)
#define I8042_CMD_DISABLE_KBD 0xadU
#define I8042_CMD_DISABLE_AUX 0xa7U
#define I8042_CMD_READ_CFG    0x20U
#define I8042_CMD_WRITE_CFG   0x60U
#define I8042_CMD_ENABLE_KBD  0xaeU
#define I8042_CMD_WRITE_AUX   0xd4U
#define I8042_CMD_ENABLE_AUX  0xa8U
#define I8042_CFG_IRQ_KBD     (1U << 0)
#define I8042_CFG_IRQ_AUX     (1U << 1)
#define I8042_CFG_KBD_NOCLOCK (1U << 4)
#define I8042_CFG_TRANSLATION (1U << 6)

#define INPUT_I8042_SPIN_LIMIT 100000U

static inline uint8_t input_inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void input_outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static struct spinlock input_lock;
static struct wait_queue input_waiters;
static struct input_queue queue;
static struct input_registry registry;
static struct scancode_decoder decoder;
static struct mouse_decoder mouse_decoder;
static uint32_t keyboard_device_id;
static uint32_t mouse_device_id;
static uint8_t live;

static int wait_buffer_empty(void) {
    for (uint32_t spins = 0; spins < INPUT_I8042_SPIN_LIMIT; ++spins) {
        if (!(input_inb(I8042_STATUS) & I8042_STATUS_IBF))
            return 0;
    }
    return -1;
}

static int wait_buffer_full(void) {
    for (uint32_t spins = 0; spins < INPUT_I8042_SPIN_LIMIT; ++spins) {
        if (input_inb(I8042_STATUS) & I8042_STATUS_OBF)
            return 0;
    }
    return -1;
}

static void drain_output(void) {
    for (uint32_t i = 0; i < 32; ++i) {
        if (!(input_inb(I8042_STATUS) & I8042_STATUS_OBF))
            break;
        (void)input_inb(I8042_DATA);
    }
}

/* Configure the controller: keyboard IRQ on, translation on (set 2 -> 1
 * codes for the decoder). The auxiliary port and its IRQ are enabled
 * later, per device, by mouse_setup. */
static int controller_setup(void) {
    uint8_t config;

    if (wait_buffer_empty() != 0)
        return -1;
    input_outb(I8042_STATUS, I8042_CMD_DISABLE_KBD);
    if (wait_buffer_empty() != 0)
        return -1;
    input_outb(I8042_STATUS, I8042_CMD_DISABLE_AUX);
    drain_output();

    if (wait_buffer_empty() != 0)
        return -1;
    input_outb(I8042_STATUS, I8042_CMD_READ_CFG);
    if (wait_buffer_full() != 0)
        return -1;
    config = input_inb(I8042_DATA);
    config |= I8042_CFG_IRQ_KBD | I8042_CFG_TRANSLATION;
    /* Keyboard line may raise IRQ1, mouse IRQ stays off, keyboard clock
     * stays enabled. */
    config &= (uint8_t)~(I8042_CFG_IRQ_AUX | I8042_CFG_KBD_NOCLOCK);
    if (wait_buffer_empty() != 0)
        return -1;
    input_outb(I8042_STATUS, I8042_CMD_WRITE_CFG);
    if (wait_buffer_empty() != 0)
        return -1;
    input_outb(I8042_DATA, config);

    if (wait_buffer_empty() != 0)
        return -1;
    input_outb(I8042_STATUS, I8042_CMD_ENABLE_KBD);
    drain_output();
    return 0;
}

static void decode_and_push(uint8_t byte) {
    struct zeroos_input_event event;
    struct input_event native;
    uint64_t flags;
    int emitted;

    flags = spin_lock_irqsave(&input_lock);
    emitted = scancode_feed(&decoder, byte, &event);
    if (emitted) {
        event.timestamp = timer_ticks();
        event.device_id = keyboard_device_id;
        native.timestamp = event.timestamp;
        native.device_id = event.device_id;
        native.x = event.x;
        native.y = event.y;
        native.value = event.value;
        native.code = event.code;
        native.kind = event.kind;
        native.flags = event.flags;
        (void)input_queue_push(&queue, &native);
    }
    spin_unlock_irqrestore(&input_lock, flags);
    if (emitted)
        (void)wait_queue_wake_one(&input_waiters);
}

static void keyboard_irq(uint8_t irq, struct interrupt_frame *frame,
                         void *context) {
    uint8_t status;
    (void)irq;
    (void)frame;
    (void)context;
    status = input_inb(I8042_STATUS);
    if (!(status & I8042_STATUS_OBF))
        return;
    if (status & I8042_STATUS_AUX)
        return; /* mouse packet: delivered by a later batch */
    decode_and_push(input_inb(I8042_DATA));
}

/* Mouse IRQ: aux bytes are never delivered on IRQ1 (the keyboard handler
 * ignores them); the i8042 routes them to IRQ12 once mouse_setup enables
 * the auxiliary interrupt. */
static void mouse_irq(uint8_t irq, struct interrupt_frame *frame,
                      void *context) {
    uint8_t status, byte;
    uint64_t flags;
    int pushed = 0;
    struct mouse_event pointer;

    (void)irq;
    (void)frame;
    (void)context;
    status = input_inb(I8042_STATUS);
    if (!(status & I8042_STATUS_OBF) || !(status & I8042_STATUS_AUX))
        return;
    byte = input_inb(I8042_DATA);

    flags = spin_lock_irqsave(&input_lock);
    (void)mouse_decoder_feed(&mouse_decoder, byte);
    while (mouse_decoder_next(&mouse_decoder, &pointer)) {
        struct input_event native;
        native.timestamp = timer_ticks();
        native.device_id = mouse_device_id;
        native.x = 0;
        native.y = 0;
        native.value = 0;
        native.code = 0;
        native.kind = INPUT_POINTER;
        native.flags = 0;
        if (pointer.kind == MOUSE_EV_MOTION) {
            /* Relative deltas in device orientation (y up). */
            native.x = pointer.dx;
            native.y = pointer.dy;
            native.value = pointer.buttons;
        } else {
            native.code = pointer.button;
            native.value = pointer.pressed;
        }
        if (input_queue_push(&queue, &native) == 0)
            pushed = 1;
    }
    spin_unlock_irqrestore(&input_lock, flags);
    if (pushed)
        (void)wait_queue_wake_one(&input_waiters);
}

/* Send one aux command (0xD4 prefix) and consume its response. 0xFE asks
 * for a bounded retry; anything else unexpected fails the bring-up. */
static int mouse_command(uint8_t command, uint8_t *response) {
    for (uint32_t attempt = 0; attempt < 3; ++attempt) {
        uint8_t reply;
        if (wait_buffer_empty() != 0)
            return -1;
        input_outb(I8042_STATUS, I8042_CMD_WRITE_AUX);
        if (wait_buffer_empty() != 0)
            return -1;
        input_outb(I8042_DATA, command);
        if (wait_buffer_full() != 0)
            return -1;
        reply = input_inb(I8042_DATA);
        if (reply == 0xfeU)
            continue; /* device asked for a resend */
        if (reply == 0xfaU) {
            *response = reply;
            return 0;
        }
        return -1;
    }
    return -1;
}

/* Enable the auxiliary device and IRQ12. Failures are reported but never
 * fail the input stack: keyboard input and certification stay authoritative. */
static const char *mouse_setup(void) {
    uint8_t config;
    uint8_t ack;

    mouse_decoder_init(&mouse_decoder);
    if (input_device_add(&registry, &mouse_device_id) != 0)
        return "device registry";
    if (wait_buffer_empty() != 0)
        return "i8042 aux enable";
    input_outb(I8042_STATUS, I8042_CMD_ENABLE_AUX);
    if (wait_buffer_empty() != 0)
        return "i8042 controller";
    input_outb(I8042_STATUS, I8042_CMD_READ_CFG);
    if (wait_buffer_full() != 0)
        return "i8042 controller";
    config = input_inb(I8042_DATA);
    config |= I8042_CFG_IRQ_AUX;
    if (wait_buffer_empty() != 0)
        return "i8042 controller";
    input_outb(I8042_STATUS, I8042_CMD_WRITE_CFG);
    if (wait_buffer_empty() != 0)
        return "i8042 controller";
    input_outb(I8042_DATA, config);

    if (mouse_command(0xf6U, &ack) != 0)
        return "mouse defaults ACK";
    if (mouse_command(0xf4U, &ack) != 0)
        return "mouse enable ACK";

    if (irq_register(12, mouse_irq, 0) != 0)
        return "IRQ12 registration";
    if (apic_controller() == ZEROOS_IRQ_CONTROLLER_LAPIC_IOAPIC) {
        if (apic_route_legacy_irq(12) != 0)
            return "IOAPIC IRQ12 route";
    } else {
        pic_unmask_irq(12);
    }
    drain_output();
    return 0;
}

int input_init(void) {
    const char *reason = 0;

    spinlock_init(&input_lock);
    wait_queue_init(&input_waiters);
    input_queue_init(&queue);
    input_registry_init(&registry);
    scancode_decoder_init(&decoder);
    keyboard_device_id = 0;
    live = 0;

    if (input_device_add(&registry, &keyboard_device_id) != 0)
        reason = "device registry";
    if (!reason && controller_setup() != 0)
        reason = "i8042 controller";
    if (!reason && irq_register(1, keyboard_irq, 0) != 0)
        reason = "IRQ1 registration";
    if (!reason) {
        if (apic_controller() == ZEROOS_IRQ_CONTROLLER_LAPIC_IOAPIC) {
            if (apic_route_legacy_irq(1) != 0)
                reason = "IOAPIC IRQ1 route";
        } else {
            pic_unmask_irq(1);
        }
    }

    /* Discard any controller self-test or boot-time bytes so Ring-3 and
     * the certification probes see a deterministic empty queue. */
    drain_output();
    {
        struct input_event leftover;
        uint64_t flags = spin_lock_irqsave(&input_lock);
        while (input_queue_pop(&queue, &leftover) == 0)
            ;
        spin_unlock_irqrestore(&input_lock, flags);
    }

    if (reason) {
        serial_write_public("ZEROOS: input stack degraded (");
        serial_write_public(reason);
        serial_write_public(").\n");
    } else {
        live = 1;
        serial_write_public("ZEROOS: PS/2 keyboard input stack ready.\n");
    }

    /* Pointer device is best-effort: its status is reported, but a
     * missing/failed mouse never blocks Ring-3 start or keyboard input. */
    {
        const char *mouse_reason = mouse_setup();
        if (mouse_reason) {
            serial_write_public("ZEROOS: mouse pointer degraded (");
            serial_write_public(mouse_reason);
            serial_write_public(").\n");
        } else {
            serial_write_public("ZEROOS: PS/2 mouse pointer ready.\n");
        }
    }
    return reason ? -1 : 0;
}

int input_live(void) {
    return live;
}

static void copy_out(const struct input_event *source,
                     struct zeroos_input_event *destination) {
    destination->timestamp = source->timestamp;
    destination->device_id = source->device_id;
    destination->x = source->x;
    destination->y = source->y;
    destination->value = source->value;
    destination->code = source->code;
    destination->kind = source->kind;
    destination->flags = source->flags;
}

int input_poll(struct zeroos_input_event *event) {
    struct input_event source;
    uint64_t flags;

    if (!event)
        return -ZEROOS_EINVAL;
    flags = spin_lock_irqsave(&input_lock);
    if (input_queue_pop(&queue, &source) != 0) {
        spin_unlock_irqrestore(&input_lock, flags);
        return -ZEROOS_EAGAIN;
    }
    copy_out(&source, event);
    spin_unlock_irqrestore(&input_lock, flags);
    return 0;
}

int input_wait(struct zeroos_input_event *event, uint64_t timeout_flags,
               uint64_t timeout_ticks) {
    uint64_t deadline = 0;

    if (!event || (timeout_flags & ~ZEROOS_WAIT_FLAG_NONBLOCK))
        return -ZEROOS_EINVAL;
    if (timeout_ticks) {
        deadline = timer_ticks() + timeout_ticks;
        if (deadline < timer_ticks())
            deadline = ~0ULL;
    }

    for (;;) {
        struct input_event source;
        uint64_t irq_flags;
        uint64_t block_flags;
        int popped;

        irq_flags = spin_lock_irqsave(&input_lock);
        popped = input_queue_pop(&queue, &source);
        if (popped == 0) {
            copy_out(&source, event);
            spin_unlock_irqrestore(&input_lock, irq_flags);
            return 0;
        }
        if (timeout_flags & ZEROOS_WAIT_FLAG_NONBLOCK) {
            spin_unlock_irqrestore(&input_lock, irq_flags);
            return -ZEROOS_EAGAIN;
        }
        if (timeout_ticks) {
            int sleep_result;
            if ((long long)(deadline - timer_ticks()) <= 0) {
                spin_unlock_irqrestore(&input_lock, irq_flags);
                return -ZEROOS_ETIMEDOUT;
            }
            spin_unlock_irqrestore(&input_lock, irq_flags);
            sleep_result = task_sleep_ticks(1);
            if (sleep_result != 0)
                return -ZEROOS_EINTR;
            continue;
        }
        /* Same lost-wakeup discipline as ipc.c: publish the waiter while
         * the queue condition lock is held, then release and commit. */
        if (wait_queue_prepare(&input_waiters, &block_flags) != 0) {
            spin_unlock_irqrestore(&input_lock, irq_flags);
            return -ZEROOS_EBUSY;
        }
        spin_unlock(&input_lock);
        /* block_flags were sampled with input_lock held (IF=0); committing
         * them would resume the woken task with interrupts disabled. Restore
         * the caller's original interrupt state instead (same fix as ipc.c
         * and process_child_wait_prepare). */
        (void)block_flags;
        if (wait_queue_commit(irq_flags) != 0)
            return -ZEROOS_EINTR;
    }
}

int input_inject(uint32_t device_id, uint16_t code, uint8_t down) {
    struct input_event event = {0};
    uint64_t flags;

    event.timestamp = timer_ticks();
    event.device_id = device_id;
    event.code = code;
    event.kind = ZEROOS_INPUT_KIND_KEY;
    event.flags = down ? ZEROOS_INPUT_FLAG_DOWN : 0;
    event.value = down ? 1 : 0;
    flags = spin_lock_irqsave(&input_lock);
    if (input_queue_push(&queue, &event) != 0) {
        spin_unlock_irqrestore(&input_lock, flags);
        return -ZEROOS_EAGAIN;
    }
    spin_unlock_irqrestore(&input_lock, flags);
    (void)wait_queue_wake_one(&input_waiters);
    return 0;
}

uint64_t input_waiter_count(void) {
    return wait_queue_count(&input_waiters);
}

uint32_t input_drain(void) {
    uint32_t dropped = 0;
    struct input_event source;
    uint64_t flags = spin_lock_irqsave(&input_lock);
    while (input_queue_pop(&queue, &source) == 0)
        ++dropped;
    spin_unlock_irqrestore(&input_lock, flags);
    return dropped;
}

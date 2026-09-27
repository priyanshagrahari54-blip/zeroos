#ifndef ZEROOS_INPUT_H
#define ZEROOS_INPUT_H

#include "syscall.h"

/*
 * Kernel input stack (Stage 5A): PS/2 i8042 keyboard controller, IRQ1
 * delivery, scancode-set-1 decoding into the bounded input_core queue and
 * event-driven wait/wake for Ring-3.
 *
 * The controller setup is best-effort like the framebuffer: a missing or
 * unresponsive i8042 degrades to a serial-reported state and never fails
 * the boot. The event queue, POLL/WAIT syscalls and injection path work
 * regardless of controller health (used by tests and later input sources).
 *
 * Waiters block on an ordinary kernel wait queue; timeout waits follow the
 * same tick-deadline loop as ipc.c. The driver never polls hardware — IRQ1
 * pushes, the queue wakes.
 */

/* One-time controller/IRQ bring-up. Returns 0 when PS/2 keyboard input is
 * live, -1 when degraded (reason printed); queue/syscalls stay usable. */
int input_init(void);
int input_live(void);

/* Nonblocking drain: 0 with *event, -EAGAIN when empty, -EINVAL. */
int input_poll(struct zeroos_input_event *event);

/* Blocking wait: flags accepts ZEROOS_WAIT_FLAG_NONBLOCK; timeout is in
 * scheduler ticks with 0 meaning forever. 0 with event, -EAGAIN, 
 * -ETIMEDOUT, -EINTR or -EINVAL. */
int input_wait(struct zeroos_input_event *event, uint64_t flags,
               uint64_t timeout_ticks);

/* Test/automation injection (future uinput-style source). Returns 0 on
 * accept, -EAGAIN when the bounded queue is full. */
int input_inject(uint32_t device_id, uint16_t code, uint8_t down);

/* Number of tasks currently blocked in input_wait (probe barrier). */
uint64_t input_waiter_count(void);

/* Discard every queued event (pre-Ring-3 drain). Returns count dropped. */
uint32_t input_drain(void);

#endif

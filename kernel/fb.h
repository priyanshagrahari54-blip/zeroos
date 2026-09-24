#ifndef ZEROOS_FB_H
#define ZEROOS_FB_H

#include "syscall.h"

/*
 * Kernel display primitive (Stage 5, graphics split A).
 *
 * The kernel discovers and reserves the firmware framebuffer and exposes a
 * read-only geometry record through the DISPLAY_INFO syscall. All desktop
 * policy — compositor, shell, frame scheduling, DPI, layout — lives in
 * userspace; this module owns nothing but the raw scanout resource.
 *
 * Discovery is best-effort: a missing or unusable framebuffer degrades to
 * the serial console path and never fails the boot.
 *
 * struct zeroos_display_info and the ZEROOS_DISPLAY_* constants are the
 * ABI definition in kernel/syscall.h (gated against the public header by
 * abi_consistency.py); this header pulls them in for fb.c.
 */

/*
 * fb_init walks the Multiboot2 boot-info tags for the framebuffer
 * descriptor (type 8), reserves any overlapping managed RAM, maps the scan
 *out pages into the kernel's supervisor MMIO window and verifies the mapping
 * by readback. Returns 0 when a linear framebuffer is live, -1 when the
 * system is running degraded (serial-only). Never panics.
 *
 * Boot-context only: runs before interrupts are enabled, so the allocator
 * reservation may use the unslammed memory_lock path like memory_init.
 */
int fb_init(uint64_t multiboot_info);

/* Geometry snapshot for the DISPLAY_INFO syscall; flags tell callers
 * whether a linear scanout buffer exists (PRESENT) or the boot degraded. */
const struct zeroos_display_info *fb_display_info(void);

#endif

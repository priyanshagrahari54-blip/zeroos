/* Minimal Ring-3 child process (Stage 5 shell).
 *
 * The shell fetches this image through the child-image syscall, spawns it
 * with SPAWN and reaps it with WAIT, so the parent/child lifecycle the
 * shell depends on is exercised by a real second process rather than a
 * simulated one.  The child does exactly three things: write one line to
 * the kernel console, return a status its parent asserts, and let the
 * runtime issue EXIT.  It touches no filesystem and no device, so it runs
 * identically whatever identity the parent has dropped to.
 */
#include <zeroos/syscall.h>
#include <stdint.h>

#define CHILD_EXIT_STATUS 7

/* A real writable object: the child must prove its RW segment is mapped
 * and usable, not just its text. */
static volatile uint64_t child_scratch;

static uint64_t child_strlen(const char *s) {
    uint64_t n = 0;
    if (!s)
        return 0;
    while (s[n] && n < 128)
        ++n;
    return n;
}

/* The kernel builds a SysV initial stack (argc, argv..., NULL, envp...,
 * NULL, auxv), so argv[1] here is the string the parent handed to SPAWN:
 * echoing it proves the argument vector really crossed the exec boundary
 * rather than being re-declared in this image. */
int child_main(uint64_t argc, const uint64_t *argv) {
    static const char message[] = "ZEROOS: shell child process alive.";
    static const char prefix[] = "ZEROOS: shell child argv1=";
    const char *argument;
    child_scratch = argc;
    (void)zeroos_write(1, message, sizeof(message) - 1);
    (void)zeroos_write(1, "\n", 1);
    if (argc >= 2 && argv && argv[1]) {
        argument = (const char *)(uintptr_t)argv[1];
        (void)zeroos_write(1, prefix, sizeof(prefix) - 1);
        (void)zeroos_write(1, argument, child_strlen(argument));
        (void)zeroos_write(1, "\n", 1);
    }
    return CHILD_EXIT_STATUS;
}

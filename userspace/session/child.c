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

#define CHILD_EXIT_STATUS 7

int child_main(void) {
    static const char message[] = "ZEROOS: shell child process alive.";
    (void)zeroos_write(1, message, sizeof(message) - 1);
    (void)zeroos_write(1, "\n", 1);
    return CHILD_EXIT_STATUS;
}

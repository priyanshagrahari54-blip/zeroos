#ifndef ZEROOS_SESSION_H
#define ZEROOS_SESSION_H

/* Stage 5 session/shell process launcher (idempotent). Called by the
 * boot monitor after the display present contract is verified; the task
 * loads the embedded Ring-3 session ELF (userspace/session/), reaps it
 * and certifies its milestones. Failures panic — the session is boot
 * certification, not a best-effort extra. */
void session_start(void);
/* 1 once the session process finished (success or reported failure). */
int session_finished(void);

#endif

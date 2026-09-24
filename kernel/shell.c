#include "shell.h"

static struct zeroos_shell shell;

int shell_system_init(void) {
    spinlock_init(&shell.lock);
    shell.state=ZEROOS_SHELL_STOPPED;
    shell.history_head=0;
    shell.history_count=0;
    shell.command_count=0;
    shell.owner_task_id=0;
    for (uint32_t i=0;i<ZEROOS_SHELL_MAX_HISTORY;++i) shell.history[i][0]=0;
    return 0;
}

int shell_execute(const char *command, uint64_t owner_task_id) {
    if (!command || owner_task_id==0) return -1;
    uint32_t len=0;
    while (command[len] && len<ZEROOS_SHELL_MAX_CMD-1) len++;
    if (len==0) return -1;
    uint64_t flags=spin_lock_irqsave(&shell.lock);
    if (shell.state!=ZEROOS_SHELL_ACTIVE && shell.state!=ZEROOS_SHELL_DORMANT) { spin_unlock_irqrestore(&shell.lock,flags); return -1; }
    uint32_t idx=shell.history_head;
    for (uint32_t i=0;i<len;++i) shell.history[idx][i]=command[i];
    shell.history[idx][len]=0;
    shell.history_head=(shell.history_head+1)%ZEROOS_SHELL_MAX_HISTORY;
    if (shell.history_count<ZEROOS_SHELL_MAX_HISTORY) shell.history_count++;
    shell.command_count++;
    shell.owner_task_id=owner_task_id;
    spin_unlock_irqrestore(&shell.lock,flags);
    return 0;
}

int shell_get_history(uint32_t index, char *out, uint64_t cap) {
    if (!out || cap==0) return -1;
    uint64_t flags=spin_lock_irqsave(&shell.lock);
    if (index>=shell.history_count) { spin_unlock_irqrestore(&shell.lock,flags); return -1; }
    uint32_t real = (shell.history_head + ZEROOS_SHELL_MAX_HISTORY - shell.history_count + index)%ZEROOS_SHELL_MAX_HISTORY;
    uint32_t i=0;
    while (i+1<cap && shell.history[real][i]) { out[i]=shell.history[real][i]; i++; }
    out[i]=0;
    spin_unlock_irqrestore(&shell.lock,flags);
    return 0;
}

int shell_set_state(enum zeroos_shell_state state) {
    uint64_t flags=spin_lock_irqsave(&shell.lock);
    shell.state=state;
    spin_unlock_irqrestore(&shell.lock,flags);
    return 0;
}

int shell_debug_validate(void) {
    uint64_t flags=spin_lock_irqsave(&shell.lock);
    if (shell.history_count>ZEROOS_SHELL_MAX_HISTORY) { spin_unlock_irqrestore(&shell.lock,flags); return -1; }
    spin_unlock_irqrestore(&shell.lock,flags);
    return 0;
}

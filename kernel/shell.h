#ifndef ZEROOS_SHELL_H
#define ZEROOS_SHELL_H

#include "types.h"
#include "sync.h"

#define ZEROOS_SHELL_MAX_CMD 128U
#define ZEROOS_SHELL_MAX_HISTORY 32U

enum zeroos_shell_state {
    ZEROOS_SHELL_STOPPED = 0,
    ZEROOS_SHELL_DORMANT,
    ZEROOS_SHELL_WARM,
    ZEROOS_SHELL_ACTIVE,
    ZEROOS_SHELL_THROTTLED,
    ZEROOS_SHELL_SUSPENDED
};

struct zeroos_shell {
    struct spinlock lock;
    enum zeroos_shell_state state;
    char history[ZEROOS_SHELL_MAX_HISTORY][ZEROOS_SHELL_MAX_CMD];
    uint32_t history_head;
    uint32_t history_count;
    uint64_t command_count;
    uint64_t owner_task_id;
};

int shell_system_init(void);
int shell_execute(const char *command, uint64_t owner_task_id);
int shell_get_history(uint32_t index, char *out, uint64_t cap);
int shell_set_state(enum zeroos_shell_state state);
int shell_debug_validate(void);

#endif

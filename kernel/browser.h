#ifndef ZEROOS_BROWSER_H
#define ZEROOS_BROWSER_H

#include "types.h"
#include "sync.h"
#include "wait.h"

#define ZEROOS_BROWSER_MAX_TABS 32U
#define ZEROOS_BROWSER_MAX_PROCESSES 16U

enum zeroos_browser_tab_state {
    ZEROOS_BROWSER_TAB_ACTIVE = 0,
    ZEROOS_BROWSER_TAB_IDLE,
    ZEROOS_BROWSER_TAB_FROZEN,
    ZEROOS_BROWSER_TAB_DISCARDED,
    ZEROOS_BROWSER_TAB_CLOSED
};

enum zeroos_browser_process_state {
    ZEROOS_BROWSER_PROC_STOPPED = 0,
    ZEROOS_BROWSER_PROC_DORMANT,
    ZEROOS_BROWSER_PROC_WARM,
    ZEROOS_BROWSER_PROC_ACTIVE,
    ZEROOS_BROWSER_PROC_THROTTLED,
    ZEROOS_BROWSER_PROC_SUSPENDED
};

struct zeroos_browser_tab {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_browser_tab_state state;
    uint64_t process_id;
    char url[256];
    char title[128];
    uint64_t last_active_ticks;
    uint64_t memory_used;
    uint8_t retained_state;
    struct spinlock lock;
    struct wait_queue state_waiters;
};

struct zeroos_browser_process {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_browser_process_state state;
    uint64_t owner_task_id;
    uint32_t tab_count;
    uint64_t memory_budget;
    struct spinlock lock;
};

int browser_system_init(void);
int browser_process_create(uint64_t owner_task_id, uint64_t *proc_id_out);
int browser_tab_create(uint64_t proc_id, const char *url, uint64_t *tab_id_out);
int browser_tab_navigate(uint64_t tab_id, const char *url);
int browser_tab_set_state(uint64_t tab_id, enum zeroos_browser_tab_state state);
int browser_tab_close(uint64_t tab_id);
int browser_process_destroy(uint64_t proc_id);
int browser_debug_validate(void);

#endif

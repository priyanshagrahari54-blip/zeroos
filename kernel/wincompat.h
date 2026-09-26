#ifndef ZEROOS_WINCOMPAT_H
#define ZEROOS_WINCOMPAT_H

#include "types.h"
#include "sync.h"

#define ZEROOS_WINCOMPAT_MAX_PROCESSES 16U
#define ZEROOS_WINCOMPAT_MAX_DLLS 64U

enum zeroos_wincompat_state {
    ZEROOS_WINCOMPAT_STOPPED = 0,
    ZEROOS_WINCOMPAT_DORMANT,
    ZEROOS_WINCOMPAT_WARM,
    ZEROOS_WINCOMPAT_ACTIVE,
    ZEROOS_WINCOMPAT_THROTTLED,
    ZEROOS_WINCOMPAT_SUSPENDED
};

enum zeroos_win_api_status {
    ZEROOS_WIN_API_SUPPORTED = 0,
    ZEROOS_WIN_API_STUB,
    ZEROOS_WIN_API_UNSUPPORTED
};

struct zeroos_win_process {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_wincompat_state state;
    uint64_t owner_task_id;
    uint32_t pid;
    char exe_path[128];
    uint64_t dll_count;
    struct spinlock lock;
};

struct zeroos_win_dll {
    uint64_t id;
    uint8_t used;
    char name[64];
    uint64_t base_address;
    uint64_t size;
    uint8_t loaded;
    struct spinlock lock;
};

int wincompat_system_init(void);
int wincompat_process_create(const char *exe_path, uint64_t owner_task_id, uint64_t *win_pid_out);
int wincompat_process_destroy(uint64_t win_pid);
int wincompat_dll_load(uint64_t win_pid, const char *dll_name, uint64_t *dll_id_out);
int wincompat_api_query(const char *api_name, enum zeroos_win_api_status *status_out);
int wincompat_debug_validate(void);

#endif

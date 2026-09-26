#include "wincompat.h"

static struct spinlock wincompat_lock;
static struct zeroos_win_process win_processes[ZEROOS_WINCOMPAT_MAX_PROCESSES];
static struct zeroos_win_dll win_dlls[ZEROOS_WINCOMPAT_MAX_DLLS];

int wincompat_system_init(void) {
    spinlock_init(&wincompat_lock);
    for (uint32_t i=0;i<ZEROOS_WINCOMPAT_MAX_PROCESSES;++i) {
        win_processes[i].used=0;
        win_processes[i].generation=0;
        spinlock_init(&win_processes[i].lock);
    }
    for (uint32_t i=0;i<ZEROOS_WINCOMPAT_MAX_DLLS;++i) {
        win_dlls[i].used=0;
        spinlock_init(&win_dlls[i].lock);
    }
    return 0;
}

int wincompat_process_create(const char *exe_path, uint64_t owner_task_id, uint64_t *win_pid_out) {
    if (!exe_path || !win_pid_out || owner_task_id==0) return -1;
    uint64_t flags=spin_lock_irqsave(&wincompat_lock);
    for (uint32_t i=0;i<ZEROOS_WINCOMPAT_MAX_PROCESSES;++i) {
        if (win_processes[i].used) continue;
        if (win_processes[i].generation==0xffffffffU) continue;
        win_processes[i].generation++;
        if (win_processes[i].generation==0) continue;
        win_processes[i].used=1;
        win_processes[i].state=ZEROOS_WINCOMPAT_DORMANT;
        win_processes[i].owner_task_id=owner_task_id;
        win_processes[i].pid=i+1;
        win_processes[i].dll_count=0;
        uint32_t n=0;
        while (n<127 && exe_path[n]) { win_processes[i].exe_path[n]=exe_path[n]; n++; }
        win_processes[i].exe_path[n]=0;
        win_processes[i].id = ((uint64_t)win_processes[i].generation<<16) | (uint64_t)(i+1);
        *win_pid_out=win_processes[i].id;
        spin_unlock_irqrestore(&wincompat_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&wincompat_lock,flags);
    return -1;
}

int wincompat_process_destroy(uint64_t win_pid) {
    uint64_t flags=spin_lock_irqsave(&wincompat_lock);
    uint32_t slot=(uint32_t)(win_pid & 0xffffULL);
    uint32_t gen=(uint32_t)(win_pid>>16);
    if (slot==0 || slot>ZEROOS_WINCOMPAT_MAX_PROCESSES || gen==0) { spin_unlock_irqrestore(&wincompat_lock,flags); return -1; }
    struct zeroos_win_process *p=&win_processes[slot-1];
    if (!p->used || p->generation!=gen) { spin_unlock_irqrestore(&wincompat_lock,flags); return -1; }
    uint64_t pflags=spin_lock_irqsave(&p->lock);
    p->used=0;
    p->state=ZEROOS_WINCOMPAT_STOPPED;
    spin_unlock_irqrestore(&p->lock,pflags);
    spin_unlock_irqrestore(&wincompat_lock,flags);
    return 0;
}

int wincompat_dll_load(uint64_t win_pid, const char *dll_name, uint64_t *dll_id_out) {
    if (!dll_name || !dll_id_out) return -1;
    uint64_t flags=spin_lock_irqsave(&wincompat_lock);
    uint32_t slot=(uint32_t)(win_pid & 0xffffULL);
    uint32_t gen=(uint32_t)(win_pid>>16);
    if (slot==0 || slot>ZEROOS_WINCOMPAT_MAX_PROCESSES || gen==0) { spin_unlock_irqrestore(&wincompat_lock,flags); return -1; }
    struct zeroos_win_process *p=&win_processes[slot-1];
    if (!p->used || p->generation!=gen) { spin_unlock_irqrestore(&wincompat_lock,flags); return -1; }
    for (uint32_t i=0;i<ZEROOS_WINCOMPAT_MAX_DLLS;++i) {
        if (win_dlls[i].used) continue;
        win_dlls[i].used=1;
        win_dlls[i].loaded=1;
        win_dlls[i].base_address=0x10000000ULL + i*0x100000ULL;
        win_dlls[i].size=0x100000ULL;
        uint32_t n=0;
        while (n<63 && dll_name[n]) { win_dlls[i].name[n]=dll_name[n]; n++; }
        win_dlls[i].name[n]=0;
        win_dlls[i].id=i+1;
        p->dll_count++;
        *dll_id_out=win_dlls[i].id;
        spin_unlock_irqrestore(&wincompat_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&wincompat_lock,flags);
    return -1;
}

int wincompat_api_query(const char *api_name, enum zeroos_win_api_status *status_out) {
    if (!api_name || !status_out) return -1;
    /* Explicit diagnostic failure for unsupported APIs */
    *status_out=ZEROOS_WIN_API_UNSUPPORTED;
    /* Known supported set */
    if (api_name[0]=='C' && api_name[1]=='r') *status_out=ZEROOS_WIN_API_SUPPORTED;
    return 0;
}

int wincompat_debug_validate(void) { return 0; }

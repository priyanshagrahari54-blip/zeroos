#ifndef ZEROOS_AI_H
#define ZEROOS_AI_H

#include "types.h"
#include "sync.h"
#include "wait.h"

#define ZEROOS_AI_MAX_SESSIONS 16U
#define ZEROOS_AI_MAX_MODELS 8U

enum zeroos_ai_state {
    ZEROOS_AI_STOPPED = 0,
    ZEROOS_AI_DORMANT,
    ZEROOS_AI_WARM,
    ZEROOS_AI_ACTIVE,
    ZEROOS_AI_THROTTLED,
    ZEROOS_AI_SUSPENDED
};

enum zeroos_ai_task_type {
    ZEROOS_AI_TASK_SEARCH = 0,
    ZEROOS_AI_TASK_FILE_SEARCH,
    ZEROOS_AI_TASK_DOC_SUMMARY,
    ZEROOS_AI_TASK_CODE_ASSIST,
    ZEROOS_AI_TASK_STUDY_ASSIST,
    ZEROOS_AI_TASK_DIAGNOSTICS,
    ZEROOS_AI_TASK_TROUBLESHOOT,
    ZEROOS_AI_TASK_PERF_ANALYSIS
};

struct zeroos_ai_model {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_ai_state state;
    char name[32];
    uint64_t memory_budget;
    uint64_t memory_used;
    uint64_t invocation_count;
    struct spinlock lock;
};

struct zeroos_ai_session {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_ai_state state;
    enum zeroos_ai_task_type task_type;
    uint64_t model_id;
    uint64_t owner_task_id;
    uint64_t permissions;
    uint8_t privacy_aware;
    struct wait_queue completion_waiters;
    struct spinlock lock;
};

int ai_system_init(void);
int ai_model_register(const char *name, uint64_t memory_budget, uint64_t *model_id_out);
int ai_session_create(uint64_t model_id, enum zeroos_ai_task_type task_type,
                      uint64_t owner_task_id, uint64_t permissions, uint64_t *session_id_out);
int ai_session_invoke(uint64_t session_id);
int ai_session_complete(uint64_t session_id);
int ai_session_destroy(uint64_t session_id);
int ai_debug_validate(void);

#endif

#ifndef ZEROOS_SEARCH_H
#define ZEROOS_SEARCH_H

#include "types.h"
#include "sync.h"

#define ZEROOS_SEARCH_MAX_INDEX 1024U
#define ZEROOS_SEARCH_MAX_QUERY 64U
#define ZEROOS_SEARCH_MAX_RESULTS 32U

enum zeroos_search_state {
    ZEROOS_SEARCH_STOPPED = 0,
    ZEROOS_SEARCH_DORMANT,
    ZEROOS_SEARCH_WARM,
    ZEROOS_SEARCH_ACTIVE,
    ZEROOS_SEARCH_THROTTLED,
    ZEROOS_SEARCH_SUSPENDED
};

struct zeroos_search_entry {
    uint64_t id;
    char name[64];
    char path[128];
    uint32_t type;
    uint8_t used;
};

struct zeroos_search_index {
    struct spinlock lock;
    enum zeroos_search_state state;
    struct zeroos_search_entry entries[ZEROOS_SEARCH_MAX_INDEX];
    uint32_t count;
    uint64_t query_count;
};

int search_system_init(void);
int search_index_add(const char *name, const char *path, uint32_t type, uint64_t *entry_id_out);
int search_query(const char *query, struct zeroos_search_entry *results, uint32_t cap, uint32_t *result_count_out);
int search_set_state(enum zeroos_search_state state);
int search_debug_validate(void);

#endif

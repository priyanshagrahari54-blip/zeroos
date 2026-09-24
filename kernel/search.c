#include "search.h"

static struct zeroos_search_index search_index;

int search_system_init(void) {
    spinlock_init(&search_index.lock);
    search_index.state=ZEROOS_SEARCH_STOPPED;
    search_index.count=0;
    search_index.query_count=0;
    for (uint32_t i=0;i<ZEROOS_SEARCH_MAX_INDEX;++i) search_index.entries[i].used=0;
    return 0;
}

int search_index_add(const char *name, const char *path, uint32_t type, uint64_t *entry_id_out) {
    if (!name || !path || !entry_id_out) return -1;
    uint64_t flags=spin_lock_irqsave(&search_index.lock);
    if (search_index.count>=ZEROOS_SEARCH_MAX_INDEX) { spin_unlock_irqrestore(&search_index.lock,flags); return -1; }
    for (uint32_t i=0;i<ZEROOS_SEARCH_MAX_INDEX;++i) {
        if (search_index.entries[i].used) continue;
        search_index.entries[i].used=1;
        search_index.entries[i].type=type;
        search_index.entries[i].id=i+1;
        uint32_t n=0;
        while (n<63 && name[n]) { search_index.entries[i].name[n]=name[n]; n++; }
        search_index.entries[i].name[n]=0;
        n=0;
        while (n<127 && path[n]) { search_index.entries[i].path[n]=path[n]; n++; }
        search_index.entries[i].path[n]=0;
        search_index.count++;
        *entry_id_out=search_index.entries[i].id;
        spin_unlock_irqrestore(&search_index.lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&search_index.lock,flags);
    return -1;
}

int search_query(const char *query, struct zeroos_search_entry *results, uint32_t cap, uint32_t *result_count_out) {
    if (!query || !results || !result_count_out || cap==0) return -1;
    uint64_t flags=spin_lock_irqsave(&search_index.lock);
    uint32_t matched=0;
    for (uint32_t i=0;i<ZEROOS_SEARCH_MAX_INDEX && matched<cap;++i) {
        if (!search_index.entries[i].used) continue;
        /* Simple substring match */
        uint32_t qlen=0; while (query[qlen] && qlen<ZEROOS_SEARCH_MAX_QUERY) qlen++;
        uint32_t nlen=0; while (search_index.entries[i].name[nlen]) nlen++;
        uint8_t found=0;
        if (qlen<=nlen) {
            for (uint32_t s=0;s+qlen<=nlen;++s) {
                uint32_t j=0;
                for (;j<qlen;++j) if (search_index.entries[i].name[s+j]!=query[j]) break;
                if (j==qlen) { found=1; break; }
            }
        }
        if (found) results[matched++]=search_index.entries[i];
    }
    *result_count_out=matched;
    search_index.query_count++;
    spin_unlock_irqrestore(&search_index.lock,flags);
    return 0;
}

int search_set_state(enum zeroos_search_state state) {
    uint64_t flags=spin_lock_irqsave(&search_index.lock);
    search_index.state=state;
    spin_unlock_irqrestore(&search_index.lock,flags);
    return 0;
}

int search_debug_validate(void) {
    uint64_t flags=spin_lock_irqsave(&search_index.lock);
    if (search_index.count>ZEROOS_SEARCH_MAX_INDEX) { spin_unlock_irqrestore(&search_index.lock,flags); return -1; }
    spin_unlock_irqrestore(&search_index.lock,flags);
    return 0;
}

#ifndef ZEROOS_DOCS_H
#define ZEROOS_DOCS_H

#include "types.h"
#include "sync.h"

#define ZEROOS_DOCS_MAX_DOCUMENTS 64U
#define ZEROOS_DOCS_MAX_TITLE 64U
#define ZEROOS_DOCS_MAX_CONTENT (16*1024U)

enum zeroos_docs_state {
    ZEROOS_DOCS_STOPPED = 0,
    ZEROOS_DOCS_DORMANT,
    ZEROOS_DOCS_WARM,
    ZEROOS_DOCS_ACTIVE,
    ZEROOS_DOCS_THROTTLED,
    ZEROOS_DOCS_SUSPENDED
};

struct zeroos_document {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    char title[ZEROOS_DOCS_MAX_TITLE];
    uint64_t content_size;
    char content[ZEROOS_DOCS_MAX_CONTENT];
    uint64_t owner_task_id;
    uint64_t created_ticks;
    uint64_t modified_ticks;
    struct spinlock lock;
};

int docs_system_init(void);
int docs_create(const char *title, const char *content, uint64_t owner_task_id, uint64_t *doc_id_out);
int docs_read(uint64_t doc_id, char *buf, uint64_t cap, uint64_t *size_out);
int docs_update(uint64_t doc_id, const char *new_content);
int docs_delete(uint64_t doc_id);
int docs_debug_validate(void);

#endif

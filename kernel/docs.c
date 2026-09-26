#include "docs.h"
#include "timer.h"

static struct spinlock docs_lock;
static struct zeroos_document docs[ZEROOS_DOCS_MAX_DOCUMENTS];

int docs_system_init(void) {
    spinlock_init(&docs_lock);
    for (uint32_t i=0;i<ZEROOS_DOCS_MAX_DOCUMENTS;++i) {
        docs[i].used=0;
        docs[i].generation=0;
        spinlock_init(&docs[i].lock);
    }
    return 0;
}

int docs_create(const char *title, const char *content, uint64_t owner_task_id, uint64_t *doc_id_out) {
    if (!title || !content || !doc_id_out || owner_task_id==0) return -1;
    uint32_t tlen=0; while (title[tlen] && tlen<ZEROOS_DOCS_MAX_TITLE-1) tlen++;
    uint32_t clen=0; while (content[clen] && clen<ZEROOS_DOCS_MAX_CONTENT-1) clen++;
    if (tlen==0) return -1;
    uint64_t flags=spin_lock_irqsave(&docs_lock);
    for (uint32_t i=0;i<ZEROOS_DOCS_MAX_DOCUMENTS;++i) {
        if (docs[i].used) continue;
        if (docs[i].generation==0xffffffffU) continue;
        docs[i].generation++;
        if (docs[i].generation==0) continue;
        docs[i].used=1;
        docs[i].owner_task_id=owner_task_id;
        docs[i].created_ticks=timer_ticks();
        docs[i].modified_ticks=docs[i].created_ticks;
        for (uint32_t k=0;k<tlen;++k) docs[i].title[k]=title[k];
        docs[i].title[tlen]=0;
        for (uint32_t k=0;k<clen;++k) docs[i].content[k]=content[k];
        docs[i].content[clen]=0;
        docs[i].content_size=clen;
        docs[i].id = ((uint64_t)docs[i].generation<<16) | (uint64_t)(i+1);
        *doc_id_out=docs[i].id;
        spin_unlock_irqrestore(&docs_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&docs_lock,flags);
    return -1;
}

int docs_read(uint64_t doc_id, char *buf, uint64_t cap, uint64_t *size_out) {
    if (!buf || !size_out || cap==0) return -1;
    uint64_t flags=spin_lock_irqsave(&docs_lock);
    uint32_t slot=(uint32_t)(doc_id & 0xffffULL);
    uint32_t gen=(uint32_t)(doc_id>>16);
    if (slot==0 || slot>ZEROOS_DOCS_MAX_DOCUMENTS || gen==0) { spin_unlock_irqrestore(&docs_lock,flags); return -1; }
    struct zeroos_document *d=&docs[slot-1];
    if (!d->used || d->generation!=gen) { spin_unlock_irqrestore(&docs_lock,flags); return -1; }
    uint64_t dflags=spin_lock_irqsave(&d->lock);
    uint64_t to_copy = d->content_size < cap-1 ? d->content_size : cap-1;
    for (uint64_t i=0;i<to_copy;++i) buf[i]=d->content[i];
    buf[to_copy]=0;
    *size_out=to_copy;
    spin_unlock_irqrestore(&d->lock,dflags);
    spin_unlock_irqrestore(&docs_lock,flags);
    return 0;
}

int docs_update(uint64_t doc_id, const char *new_content) {
    if (!new_content) return -1;
    uint32_t clen=0; while (new_content[clen] && clen<ZEROOS_DOCS_MAX_CONTENT-1) clen++;
    uint64_t flags=spin_lock_irqsave(&docs_lock);
    uint32_t slot=(uint32_t)(doc_id & 0xffffULL);
    uint32_t gen=(uint32_t)(doc_id>>16);
    if (slot==0 || slot>ZEROOS_DOCS_MAX_DOCUMENTS || gen==0) { spin_unlock_irqrestore(&docs_lock,flags); return -1; }
    struct zeroos_document *d=&docs[slot-1];
    if (!d->used || d->generation!=gen) { spin_unlock_irqrestore(&docs_lock,flags); return -1; }
    uint64_t dflags=spin_lock_irqsave(&d->lock);
    for (uint32_t k=0;k<clen;++k) d->content[k]=new_content[k];
    d->content[clen]=0;
    d->content_size=clen;
    d->modified_ticks=timer_ticks();
    spin_unlock_irqrestore(&d->lock,dflags);
    spin_unlock_irqrestore(&docs_lock,flags);
    return 0;
}

int docs_delete(uint64_t doc_id) {
    uint64_t flags=spin_lock_irqsave(&docs_lock);
    uint32_t slot=(uint32_t)(doc_id & 0xffffULL);
    uint32_t gen=(uint32_t)(doc_id>>16);
    if (slot==0 || slot>ZEROOS_DOCS_MAX_DOCUMENTS || gen==0) { spin_unlock_irqrestore(&docs_lock,flags); return -1; }
    struct zeroos_document *d=&docs[slot-1];
    if (!d->used || d->generation!=gen) { spin_unlock_irqrestore(&docs_lock,flags); return -1; }
    uint64_t dflags=spin_lock_irqsave(&d->lock);
    d->used=0;
    spin_unlock_irqrestore(&d->lock,dflags);
    spin_unlock_irqrestore(&docs_lock,flags);
    return 0;
}

int docs_debug_validate(void) {
    uint64_t flags=spin_lock_irqsave(&docs_lock);
    for (uint32_t i=0;i<ZEROOS_DOCS_MAX_DOCUMENTS;++i) if (docs[i].used && docs[i].content_size>=ZEROOS_DOCS_MAX_CONTENT) { spin_unlock_irqrestore(&docs_lock,flags); return -1; }
    spin_unlock_irqrestore(&docs_lock,flags);
    return 0;
}

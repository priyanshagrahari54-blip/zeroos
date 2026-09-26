#include "settings.h"
#include "timer.h"

static struct zeroos_settings_store settings_store;

int settings_system_init(void) {
    spinlock_init(&settings_store.lock);
    settings_store.state=ZEROOS_SETTINGS_STOPPED;
    settings_store.count=0;
    settings_store.global_version=1;
    for (uint32_t i=0;i<ZEROOS_SETTINGS_MAX_KEYS;++i) settings_store.entries[i].used=0;
    return 0;
}

int settings_set(const char *key, const char *value) {
    if (!key || !value) return -1;
    uint32_t klen=0; while (key[klen] && klen<ZEROOS_SETTINGS_MAX_KEY_LEN) klen++;
    if (klen==0 || klen>=ZEROOS_SETTINGS_MAX_KEY_LEN) return -1;
    uint32_t vlen=0; while (value[vlen] && vlen<ZEROOS_SETTINGS_MAX_VALUE_LEN) vlen++;
    if (vlen>=ZEROOS_SETTINGS_MAX_VALUE_LEN) return -1;
    uint64_t flags=spin_lock_irqsave(&settings_store.lock);
    for (uint32_t i=0;i<ZEROOS_SETTINGS_MAX_KEYS;++i) {
        if (!settings_store.entries[i].used) continue;
        uint32_t j=0;
        for (;j<klen;++j) if (settings_store.entries[i].key[j]!=key[j]) break;
        if (j==klen && settings_store.entries[i].key[klen]==0) {
            for (uint32_t k=0;k<=vlen;++k) settings_store.entries[i].value[k]=value[k];
            settings_store.entries[i].version=settings_store.global_version++;
            settings_store.entries[i].last_modified=timer_ticks();
            spin_unlock_irqrestore(&settings_store.lock,flags);
            return 0;
        }
    }
    for (uint32_t i=0;i<ZEROOS_SETTINGS_MAX_KEYS;++i) {
        if (settings_store.entries[i].used) continue;
        settings_store.entries[i].used=1;
        for (uint32_t k=0;k<=klen;++k) settings_store.entries[i].key[k]=key[k];
        for (uint32_t k=0;k<=vlen;++k) settings_store.entries[i].value[k]=value[k];
        settings_store.entries[i].version=settings_store.global_version++;
        settings_store.entries[i].last_modified=timer_ticks();
        settings_store.count++;
        spin_unlock_irqrestore(&settings_store.lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&settings_store.lock,flags);
    return -1;
}

int settings_get(const char *key, char *value_out, uint64_t cap) {
    if (!key || !value_out || cap==0) return -1;
    uint64_t flags=spin_lock_irqsave(&settings_store.lock);
    for (uint32_t i=0;i<ZEROOS_SETTINGS_MAX_KEYS;++i) {
        if (!settings_store.entries[i].used) continue;
        uint32_t j=0;
        while (key[j] && settings_store.entries[i].key[j]==key[j]) j++;
        if (key[j]==0 && settings_store.entries[i].key[j]==0) {
            uint32_t k=0;
            while (k+1<cap && settings_store.entries[i].value[k]) { value_out[k]=settings_store.entries[i].value[k]; k++; }
            value_out[k]=0;
            spin_unlock_irqrestore(&settings_store.lock,flags);
            return 0;
        }
    }
    spin_unlock_irqrestore(&settings_store.lock,flags);
    return -1;
}

int settings_delete(const char *key) {
    if (!key) return -1;
    uint64_t flags=spin_lock_irqsave(&settings_store.lock);
    for (uint32_t i=0;i<ZEROOS_SETTINGS_MAX_KEYS;++i) {
        if (!settings_store.entries[i].used) continue;
        uint32_t j=0;
        while (key[j] && settings_store.entries[i].key[j]==key[j]) j++;
        if (key[j]==0 && settings_store.entries[i].key[j]==0) {
            settings_store.entries[i].used=0;
            settings_store.count--;
            spin_unlock_irqrestore(&settings_store.lock,flags);
            return 0;
        }
    }
    spin_unlock_irqrestore(&settings_store.lock,flags);
    return -1;
}

int settings_set_state(enum zeroos_settings_state state) {
    uint64_t flags=spin_lock_irqsave(&settings_store.lock);
    settings_store.state=state;
    spin_unlock_irqrestore(&settings_store.lock,flags);
    return 0;
}

int settings_debug_validate(void) {
    uint64_t flags=spin_lock_irqsave(&settings_store.lock);
    if (settings_store.count>ZEROOS_SETTINGS_MAX_KEYS) { spin_unlock_irqrestore(&settings_store.lock,flags); return -1; }
    spin_unlock_irqrestore(&settings_store.lock,flags);
    return 0;
}

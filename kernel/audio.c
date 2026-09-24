#include "audio.h"

static struct spinlock audio_lock;
static struct zeroos_audio_device audio_devices[ZEROOS_AUDIO_MAX_DEVICES];
static struct zeroos_audio_stream audio_streams[ZEROOS_AUDIO_MAX_STREAMS];

int audio_system_init(void) {
    spinlock_init(&audio_lock);
    for (uint32_t i=0;i<ZEROOS_AUDIO_MAX_DEVICES;++i) {
        audio_devices[i].used=0;
        audio_devices[i].generation=0;
        audio_devices[i].state=ZEROOS_AUDIO_STOPPED;
        spinlock_init(&audio_devices[i].lock);
    }
    for (uint32_t i=0;i<ZEROOS_AUDIO_MAX_STREAMS;++i) {
        audio_streams[i].used=0;
        audio_streams[i].generation=0;
        audio_streams[i].state=ZEROOS_AUDIO_STOPPED;
        spinlock_init(&audio_streams[i].lock);
        wait_queue_init(&audio_streams[i].data_waiters);
        wait_queue_init(&audio_streams[i].space_waiters);
    }
    return 0;
}

int audio_device_register(const char *name, uint32_t max_channels, uint32_t max_sample_rate,
                          uint64_t *device_id_out) {
    if (!name || !device_id_out || max_channels==0 || max_sample_rate==0) return -1;
    uint64_t flags=spin_lock_irqsave(&audio_lock);
    for (uint32_t i=0;i<ZEROOS_AUDIO_MAX_DEVICES;++i) {
        if (audio_devices[i].used) continue;
        if (audio_devices[i].generation==0xffffffffU) continue;
        audio_devices[i].generation++;
        if (audio_devices[i].generation==0) continue;
        audio_devices[i].used=1;
        audio_devices[i].state=ZEROOS_AUDIO_DORMANT;
        audio_devices[i].max_channels=max_channels;
        audio_devices[i].max_sample_rate=max_sample_rate;
        audio_devices[i].buffer_size=ZEROOS_AUDIO_MAX_BUFFER;
        uint32_t n=0;
        while (n<31 && name[n]) { audio_devices[i].name[n]=name[n]; n++; }
        audio_devices[i].name[n]=0;
        audio_devices[i].id = ((uint64_t)audio_devices[i].generation<<16) | (uint64_t)(i+1);
        *device_id_out=audio_devices[i].id;
        spin_unlock_irqrestore(&audio_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&audio_lock,flags);
    return -1;
}

int audio_stream_create(uint64_t device_id, enum zeroos_audio_direction dir,
                        enum zeroos_audio_format fmt, uint32_t sample_rate,
                        uint32_t channels, uint64_t owner_task_id, uint64_t *stream_id_out) {
    if (!stream_id_out || sample_rate==0 || channels==0 || owner_task_id==0) return -1;
    uint64_t flags=spin_lock_irqsave(&audio_lock);
    uint32_t dslot=(uint32_t)(device_id & 0xffffULL);
    uint32_t dgen=(uint32_t)(device_id>>16);
    if (dslot==0 || dslot>ZEROOS_AUDIO_MAX_DEVICES || dgen==0) { spin_unlock_irqrestore(&audio_lock,flags); return -1; }
    struct zeroos_audio_device *dev=&audio_devices[dslot-1];
    if (!dev->used || dev->generation!=dgen) { spin_unlock_irqrestore(&audio_lock,flags); return -1; }
    for (uint32_t i=0;i<ZEROOS_AUDIO_MAX_STREAMS;++i) {
        if (audio_streams[i].used) continue;
        if (audio_streams[i].generation==0xffffffffU) continue;
        audio_streams[i].generation++;
        if (audio_streams[i].generation==0) continue;
        audio_streams[i].used=1;
        audio_streams[i].state=ZEROOS_AUDIO_DORMANT;
        audio_streams[i].direction=dir;
        audio_streams[i].format=fmt;
        audio_streams[i].sample_rate=sample_rate;
        audio_streams[i].channels=channels;
        audio_streams[i].device_id=device_id;
        audio_streams[i].owner_task_id=owner_task_id;
        audio_streams[i].head=audio_streams[i].tail=audio_streams[i].count=0;
        audio_streams[i].underruns=audio_streams[i].overruns=0;
        audio_streams[i].id = ((uint64_t)audio_streams[i].generation<<16) | (uint64_t)(i+1);
        *stream_id_out=audio_streams[i].id;
        spin_unlock_irqrestore(&audio_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&audio_lock,flags);
    return -1;
}

int audio_stream_write(uint64_t stream_id, const void *data, uint64_t len, uint64_t timeout_ticks) {
    if (!data || len==0 || len>ZEROOS_AUDIO_MAX_BUFFER) return -1;
    (void)timeout_ticks;
    uint64_t flags=spin_lock_irqsave(&audio_lock);
    uint32_t slot=(uint32_t)(stream_id & 0xffffULL);
    uint32_t gen=(uint32_t)(stream_id>>16);
    if (slot==0 || slot>ZEROOS_AUDIO_MAX_STREAMS || gen==0) { spin_unlock_irqrestore(&audio_lock,flags); return -1; }
    struct zeroos_audio_stream *s=&audio_streams[slot-1];
    if (!s->used || s->generation!=gen) { spin_unlock_irqrestore(&audio_lock,flags); return -1; }
    uint64_t sflags=spin_lock_irqsave(&s->lock);
    if (s->direction!=ZEROOS_AUDIO_PLAYBACK) { spin_unlock_irqrestore(&s->lock,sflags); spin_unlock_irqrestore(&audio_lock,flags); return -1; }
    uint64_t free_space = ZEROOS_AUDIO_MAX_BUFFER - s->count;
    uint64_t to_copy = len < free_space ? len : free_space;
    if (to_copy==0) {
        s->overruns++;
        spin_unlock_irqrestore(&s->lock,sflags);
        spin_unlock_irqrestore(&audio_lock,flags);
        return -1;
    }
    for (uint64_t i=0;i<to_copy;++i) {
        s->buffer[(s->tail+i)%ZEROOS_AUDIO_MAX_BUFFER]=((const uint8_t*)data)[i];
    }
    s->tail=(s->tail+to_copy)%ZEROOS_AUDIO_MAX_BUFFER;
    s->count+=to_copy;
    spin_unlock_irqrestore(&s->lock,sflags);
    (void)wait_queue_wake_all(&s->data_waiters);
    spin_unlock_irqrestore(&audio_lock,flags);
    return (int)to_copy;
}

int audio_stream_read(uint64_t stream_id, void *data, uint64_t cap, uint64_t *len_out, uint64_t timeout_ticks) {
    if (!data || !len_out || cap==0) return -1;
    (void)timeout_ticks;
    uint64_t flags=spin_lock_irqsave(&audio_lock);
    uint32_t slot=(uint32_t)(stream_id & 0xffffULL);
    uint32_t gen=(uint32_t)(stream_id>>16);
    if (slot==0 || slot>ZEROOS_AUDIO_MAX_STREAMS || gen==0) { spin_unlock_irqrestore(&audio_lock,flags); return -1; }
    struct zeroos_audio_stream *s=&audio_streams[slot-1];
    if (!s->used || s->generation!=gen) { spin_unlock_irqrestore(&audio_lock,flags); return -1; }
    uint64_t sflags=spin_lock_irqsave(&s->lock);
    if (s->direction!=ZEROOS_AUDIO_CAPTURE) { spin_unlock_irqrestore(&s->lock,sflags); spin_unlock_irqrestore(&audio_lock,flags); return -1; }
    uint64_t to_copy = s->count < cap ? s->count : cap;
    if (to_copy==0) { spin_unlock_irqrestore(&s->lock,sflags); spin_unlock_irqrestore(&audio_lock,flags); return -1; }
    for (uint64_t i=0;i<to_copy;++i) {
        ((uint8_t*)data)[i]=s->buffer[(s->head+i)%ZEROOS_AUDIO_MAX_BUFFER];
    }
    s->head=(s->head+to_copy)%ZEROOS_AUDIO_MAX_BUFFER;
    s->count-=to_copy;
    *len_out=to_copy;
    spin_unlock_irqrestore(&s->lock,sflags);
    (void)wait_queue_wake_all(&s->space_waiters);
    spin_unlock_irqrestore(&audio_lock,flags);
    return (int)to_copy;
}

int audio_stream_set_state(uint64_t stream_id, enum zeroos_audio_state state) {
    uint64_t flags=spin_lock_irqsave(&audio_lock);
    uint32_t slot=(uint32_t)(stream_id & 0xffffULL);
    uint32_t gen=(uint32_t)(stream_id>>16);
    if (slot==0 || slot>ZEROOS_AUDIO_MAX_STREAMS || gen==0) { spin_unlock_irqrestore(&audio_lock,flags); return -1; }
    struct zeroos_audio_stream *s=&audio_streams[slot-1];
    if (!s->used || s->generation!=gen) { spin_unlock_irqrestore(&audio_lock,flags); return -1; }
    uint64_t sflags=spin_lock_irqsave(&s->lock);
    s->state=state;
    spin_unlock_irqrestore(&s->lock,sflags);
    spin_unlock_irqrestore(&audio_lock,flags);
    return 0;
}

int audio_stream_close(uint64_t stream_id) {
    uint64_t flags=spin_lock_irqsave(&audio_lock);
    uint32_t slot=(uint32_t)(stream_id & 0xffffULL);
    uint32_t gen=(uint32_t)(stream_id>>16);
    if (slot==0 || slot>ZEROOS_AUDIO_MAX_STREAMS || gen==0) { spin_unlock_irqrestore(&audio_lock,flags); return -1; }
    struct zeroos_audio_stream *s=&audio_streams[slot-1];
    if (!s->used || s->generation!=gen) { spin_unlock_irqrestore(&audio_lock,flags); return -1; }
    uint64_t sflags=spin_lock_irqsave(&s->lock);
    s->used=0;
    s->state=ZEROOS_AUDIO_STOPPED;
    (void)wait_queue_wake_all(&s->data_waiters);
    (void)wait_queue_wake_all(&s->space_waiters);
    spin_unlock_irqrestore(&s->lock,sflags);
    spin_unlock_irqrestore(&audio_lock,flags);
    return 0;
}

int audio_debug_validate(void) {
    uint64_t flags=spin_lock_irqsave(&audio_lock);
    for (uint32_t i=0;i<ZEROOS_AUDIO_MAX_STREAMS;++i) if (audio_streams[i].used && audio_streams[i].count>ZEROOS_AUDIO_MAX_BUFFER) { spin_unlock_irqrestore(&audio_lock,flags); return -1; }
    spin_unlock_irqrestore(&audio_lock,flags);
    return 0;
}

#ifndef ZEROOS_AUDIO_H
#define ZEROOS_AUDIO_H

#include "types.h"
#include "sync.h"
#include "wait.h"

#define ZEROOS_AUDIO_MAX_DEVICES 4U
#define ZEROOS_AUDIO_MAX_STREAMS 16U
#define ZEROOS_AUDIO_MAX_BUFFER (64*1024U)

enum zeroos_audio_state {
    ZEROOS_AUDIO_STOPPED = 0,
    ZEROOS_AUDIO_DORMANT,
    ZEROOS_AUDIO_WARM,
    ZEROOS_AUDIO_ACTIVE,
    ZEROOS_AUDIO_THROTTLED,
    ZEROOS_AUDIO_SUSPENDED
};

enum zeroos_audio_direction {
    ZEROOS_AUDIO_PLAYBACK = 0,
    ZEROOS_AUDIO_CAPTURE
};

enum zeroos_audio_format {
    ZEROOS_AUDIO_FMT_S16_LE = 0,
    ZEROOS_AUDIO_FMT_S24_LE,
    ZEROOS_AUDIO_FMT_S32_LE,
    ZEROOS_AUDIO_FMT_FLOAT
};

struct zeroos_audio_device {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_audio_state state;
    char name[32];
    uint32_t max_channels;
    uint32_t max_sample_rate;
    uint64_t buffer_size;
    struct spinlock lock;
};

struct zeroos_audio_stream {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_audio_state state;
    enum zeroos_audio_direction direction;
    enum zeroos_audio_format format;
    uint32_t sample_rate;
    uint32_t channels;
    uint64_t device_id;
    uint64_t owner_task_id;
    uint8_t buffer[ZEROOS_AUDIO_MAX_BUFFER];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    struct spinlock lock;
    struct wait_queue data_waiters;
    struct wait_queue space_waiters;
    uint64_t underruns;
    uint64_t overruns;
};

int audio_system_init(void);
int audio_device_register(const char *name, uint32_t max_channels, uint32_t max_sample_rate,
                          uint64_t *device_id_out);
int audio_stream_create(uint64_t device_id, enum zeroos_audio_direction dir,
                        enum zeroos_audio_format fmt, uint32_t sample_rate,
                        uint32_t channels, uint64_t owner_task_id, uint64_t *stream_id_out);
int audio_stream_write(uint64_t stream_id, const void *data, uint64_t len, uint64_t timeout_ticks);
int audio_stream_read(uint64_t stream_id, void *data, uint64_t cap, uint64_t *len_out, uint64_t timeout_ticks);
int audio_stream_set_state(uint64_t stream_id, enum zeroos_audio_state state);
int audio_stream_close(uint64_t stream_id);
int audio_debug_validate(void);

#endif

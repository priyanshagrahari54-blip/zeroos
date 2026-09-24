#include <zeroos/desktop/compositor.h>

/* Frame pacing -------------------------------------------------------- */

static uint64_t frame_interval_from_refresh(uint32_t refresh_mhz,
                                            uint32_t low_power,
                                            uint32_t max_fps_low_power) {
    uint64_t interval;
    if (refresh_mhz == 0)
        refresh_mhz = 60000U; /* declared fallback, not a hardware claim */
    interval = (1000000000ULL * 1000ULL) / refresh_mhz;
    if (low_power) {
        if (max_fps_low_power) {
            uint64_t cap_interval = 1000000000ULL / max_fps_low_power;
            if (cap_interval > interval)
                interval = cap_interval;
        } else {
            interval *= 2ULL;
        }
    }
    return interval ? interval : 1000000ULL;
}

void zd_compositor_init(struct zd_compositor *compositor, struct zd_wm *wm,
                        const struct zd_frame_policy *policy) {
    if (!compositor)
        return;
    zd_memset(compositor, 0, sizeof(*compositor));
    compositor->wm = wm;
    if (policy)
        compositor->policy = *policy;
    if (compositor->policy.refresh_mhz == 0)
        compositor->policy.refresh_mhz = 60000U;
    if (compositor->policy.max_fps_low_power == 0)
        compositor->policy.max_fps_low_power = 30;
    compositor->frame_interval_ns =
        frame_interval_from_refresh(compositor->policy.refresh_mhz,
                                    compositor->policy.low_power,
                                    compositor->policy.max_fps_low_power);
    compositor->cache_budget_bytes = 4ULL * 1024ULL * 1024ULL;
}

void zd_compositor_set_policy(struct zd_compositor *compositor,
                              const struct zd_frame_policy *policy) {
    if (!compositor || !policy)
        return;
    compositor->policy = *policy;
    if (compositor->policy.refresh_mhz == 0)
        compositor->policy.refresh_mhz = 60000U;
    if (compositor->policy.max_fps_low_power == 0)
        compositor->policy.max_fps_low_power = 30;
    compositor->frame_interval_ns =
        frame_interval_from_refresh(compositor->policy.refresh_mhz,
                                    compositor->policy.low_power,
                                    compositor->policy.max_fps_low_power);
}

uint64_t zd_compositor_frame_interval_ns(const struct zd_compositor *c) {
    return c ? c->frame_interval_ns : 0;
}

int zd_compositor_set_pixel_source(struct zd_compositor *compositor,
                                   zd_pixel_source_fn source, void *context) {
    if (!compositor || !source)
        return -ZD_EINVAL;
    compositor->pixel_source = source;
    compositor->pixel_context = context;
    return 0;
}

int zd_compositor_add_listener(struct zd_compositor *compositor,
                               void (*on_present)(void *context,
                                                  uint64_t frame,
                                                  struct zd_rect damaged_area),
                               void *context) {
    if (!compositor || !on_present)
        return -ZD_EINVAL;
    if (compositor->listener_count >= ZD_COMPOSITOR_MAX_LISTENERS)
        return -ZD_ENOSPC;
    compositor->listeners[compositor->listener_count].on_present = on_present;
    compositor->listeners[compositor->listener_count].context = context;
    ++compositor->listener_count;
    return 0;
}

void zd_compositor_set_cache_budget(struct zd_compositor *compositor,
                                    uint64_t bytes) {
    if (compositor)
        compositor->cache_budget_bytes = bytes;
}

/* Scene ---------------------------------------------------------------- */

void zd_compositor_sync_scene(struct zd_compositor *compositor) {
    const struct zd_wm *wm;
    uint32_t index;
    uint32_t node_count = 0;

    if (!compositor || !compositor->wm)
        return;
    wm = compositor->wm;
    for (index = 0; index < wm->stacking_count &&
         node_count < ZD_MAX_SCENE_NODES; ++index) {
        const struct zd_window *window =
            zd_wm_window_const(wm, wm->stacking[index]);
        const struct zd_monitor *monitor;
        struct zd_scene_node *node;
        if (!window || !window->mapped ||
            window->state == ZD_WINDOW_MINIMIZED ||
            window->workspace != wm->active_workspace)
            continue;
        monitor = zd_wm_monitor(wm, window->monitor_id);
        if (!monitor)
            continue;
        node = &compositor->nodes[node_count++];
        node->id = window->id;
        node->physical = zd_wm_logical_to_physical(monitor, window->logical);
        node->enabled = 1;
        node->is_window = 1;
        node->workspace = window->workspace;
        node->focused = window->keyboard_focused;
        node->opacity = 255;
        node->resource_key = window->buffer_generation;
    }
    compositor->node_count = node_count;
    compositor->scene_valid = 1;
}

static const struct zd_scene_node *find_node(const struct zd_compositor *c,
                                             zd_window_id id) {
    uint32_t index;
    for (index = 0; index < c->node_count; ++index)
        if (c->nodes[index].id == id && c->nodes[index].enabled)
            return &c->nodes[index];
    return (const struct zd_scene_node *)0;
}

static struct zd_rect union_rect(struct zd_rect a, struct zd_rect b) {
    struct zd_rect out;
    int32_t x0 = a.x < b.x ? a.x : b.x;
    int32_t y0 = a.y < b.y ? a.y : b.y;
    int32_t x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
    int32_t y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
    out.x = x0;
    out.y = y0;
    out.w = x1 - x0;
    out.h = y1 - y0;
    return out;
}

static struct zd_rect screen_bounds(const struct zd_wm *wm) {
    struct zd_rect screen;
    uint32_t index;
    if (!wm || wm->monitor_count == 0) {
        struct zd_rect empty = {0, 0, 0, 0};
        return empty;
    }
    screen = wm->monitors[0].bounds;
    for (index = 1; index < wm->monitor_count; ++index)
        screen = union_rect(screen, wm->monitors[index].bounds);
    return screen;
}

static void damage_push(struct zd_compositor *compositor, zd_window_id owner,
                        struct zd_rect rect) {
    uint32_t index;
    uint32_t merges = 0;

    rect = zd_rect_intersection(rect, screen_bounds(compositor->wm));
    if (zd_rect_empty(rect))
        return;

    /* Merge with same-owner overlapping entries to keep the table small. */
    for (index = 0; index < compositor->damage_count;) {
        if (compositor->damage[index].owner == owner &&
            zd_rect_intersects(compositor->damage[index].rect, rect)) {
            rect = union_rect(compositor->damage[index].rect, rect);
            for (; index + 1U < compositor->damage_count; ++index)
                compositor->damage[index] = compositor->damage[index + 1U];
            --compositor->damage_count;
            merges = 1;
            index = 0;
            continue;
        }
        ++index;
    }
    (void)merges;

    if (compositor->damage_count < ZD_MAX_DAMAGE_RECTS) {
        compositor->damage[compositor->damage_count].owner = owner;
        compositor->damage[compositor->damage_count].rect = rect;
        ++compositor->damage_count;
        compositor->damage_pixels_pending += zd_rect_area(rect);
        ++compositor->stats.damage_rects_submitted;
        return;
    }

    /* Table exhausted: fold everything into one bounding rect owned by the
     * desktop (bounded memory, still correct — just less culled). */
    {
        struct zd_rect bound = compositor->damage[0].rect;
        for (index = 1; index < compositor->damage_count; ++index)
            bound = union_rect(bound, compositor->damage[index].rect);
        bound = union_rect(bound, rect);
        compositor->damage[0].owner = ZD_INVALID_WINDOW;
        compositor->damage[0].rect = bound;
        compositor->damage_count = 1;
        compositor->damage_pixels_pending = zd_rect_area(bound);
        ++compositor->stats.damage_rects_submitted;
    }
}

int zd_compositor_damage(struct zd_compositor *compositor, zd_window_id id,
                         struct zd_rect physical) {
    const struct zd_scene_node *node;
    struct zd_rect clipped;

    if (!compositor || id == ZD_INVALID_WINDOW || zd_rect_empty(physical))
        return -ZD_EINVAL;
    if (!compositor->scene_valid)
        zd_compositor_sync_scene(compositor);
    node = find_node(compositor, id);
    if (!node)
        return -ZD_ENOENT;
    clipped = zd_rect_intersection(physical, node->physical);
    if (zd_rect_empty(clipped))
        return 0;
    damage_push(compositor, id, clipped);
    return 0;
}

int zd_compositor_damage_full(struct zd_compositor *compositor,
                              zd_window_id id) {
    const struct zd_scene_node *node;
    if (!compositor || id == ZD_INVALID_WINDOW)
        return -ZD_EINVAL;
    if (!compositor->scene_valid)
        zd_compositor_sync_scene(compositor);
    node = find_node(compositor, id);
    if (!node)
        return -ZD_ENOENT;
    damage_push(compositor, id, node->physical);
    return 0;
}

/* Subtract occluder `cut` from `in`; writes up to `capacity` disjoint
 * pieces. Returns the number of pieces written. */
static uint32_t subtract_rect(struct zd_rect in, struct zd_rect cut,
                              struct zd_rect *out, uint32_t capacity) {
    struct zd_rect clipped;
    uint32_t count = 0;
    int32_t right;
    int32_t bottom;

    if (capacity == 0 || zd_rect_empty(in))
        return 0;
    if (zd_rect_empty(cut)) {
        out[count++] = in;
        return count;
    }
    clipped = zd_rect_intersection(in, cut);
    if (zd_rect_empty(clipped)) {
        out[count++] = in;
        return count;
    }
    if (in.y < clipped.y && count < capacity) {
        struct zd_rect piece = {in.x, in.y, in.w, clipped.y - in.y};
        out[count++] = piece;
    }
    bottom = in.y + in.h;
    if (bottom > clipped.y + clipped.h && count < capacity) {
        struct zd_rect piece = {in.x, clipped.y + clipped.h, in.w,
                                bottom - (clipped.y + clipped.h)};
        out[count++] = piece;
    }
    if (in.x < clipped.x && count < capacity) {
        struct zd_rect piece = {in.x, clipped.y, clipped.x - in.x, clipped.h};
        out[count++] = piece;
    }
    right = in.x + in.w;
    if (right > clipped.x + clipped.w && count < capacity) {
        struct zd_rect piece = {clipped.x + clipped.w, clipped.y,
                                right - (clipped.x + clipped.w), clipped.h};
        out[count++] = piece;
    }
    return count;
}

static int32_t node_stacking_index(const struct zd_compositor *c,
                                   zd_window_id id) {
    uint32_t index;
    for (index = 0; index < c->node_count; ++index)
        if (c->nodes[index].id == id)
            return (int32_t)index;
    return -1;
}

uint32_t zd_compositor_visible_damage(struct zd_compositor *compositor,
                                      struct zd_rect *out, uint32_t capacity) {
    uint32_t damage_index;
    uint32_t out_count = 0;

    if (!compositor || !out || capacity == 0)
        return 0;
    if (!compositor->scene_valid)
        zd_compositor_sync_scene(compositor);

    for (damage_index = 0; damage_index < compositor->damage_count;
         ++damage_index) {
        struct zd_rect pieces[4];
        uint32_t piece_count = 1;
        uint32_t node_index;
        int32_t owner_index =
            compositor->damage[damage_index].owner == ZD_INVALID_WINDOW ?
            (int32_t)compositor->node_count : /* desktop damage: culled by all */
            node_stacking_index(compositor,
                                compositor->damage[damage_index].owner);
        if (owner_index < 0)
            owner_index = 0;
        pieces[0] = compositor->damage[damage_index].rect;

        /* Subtract every opaque window stacked strictly above the owner. */
        for (node_index = 0; node_index < compositor->node_count;
             ++node_index) {
            struct zd_rect remain[4];
            uint32_t remain_count = 0;
            uint32_t piece;
            const struct zd_scene_node *occluder =
                &compositor->nodes[node_index];
            if (!occluder->enabled || occluder->opacity < 128)
                continue;
            if (!occluder->is_window)
                continue;
            if ((int32_t)node_index <= owner_index)
                continue;
            for (piece = 0; piece < piece_count; ++piece) {
                struct zd_rect parts[4];
                uint32_t part;
                uint32_t part_count =
                    subtract_rect(pieces[piece], occluder->physical, parts, 4);
                for (part = 0; part < part_count &&
                                remain_count < ZD_ARRAY_COUNT(remain);
                     ++part)
                    remain[remain_count++] = parts[part];
            }
            piece_count = remain_count;
            for (piece = 0; piece < piece_count; ++piece)
                pieces[piece] = remain[piece];
            if (piece_count == 0)
                break;
        }
        for (node_index = 0; node_index < piece_count &&
             out_count < capacity; ++node_index)
            if (!zd_rect_empty(pieces[node_index]))
                out[out_count++] = pieces[node_index];
    }
    return out_count;
}

/* Frame scheduling ----------------------------------------------------- */

int zd_compositor_begin_frame(struct zd_compositor *compositor,
                              uint64_t now_ns) {
    if (!compositor)
        return -ZD_EINVAL;
    if (compositor->damage_count == 0) {
        ++compositor->stats.frames_skipped_not_dirty;
        return -ZD_EAGAIN;
    }
    if (now_ns < compositor->next_present_ns) {
        ++compositor->stats.frames_skipped_paced;
        return -ZD_EAGAIN;
    }
    return 0;
}

uint64_t zd_compositor_time_to_present(const struct zd_compositor *compositor,
                                       uint64_t now_ns) {
    if (!compositor)
        return 0;
    if (now_ns >= compositor->next_present_ns)
        return 0;
    return compositor->next_present_ns - now_ns;
}

/* Resource cache (LRU, byte budgeted) ---------------------------------- */

static void cache_evict_lru(struct zd_compositor *compositor) {
    uint32_t index;
    uint32_t victim = ZD_MAX_SCENE_NODES;
    uint64_t oldest = ~0ULL;
    for (index = 0; index < ZD_MAX_SCENE_NODES; ++index)
        if (compositor->cache[index].valid &&
            compositor->cache[index].last_used < oldest) {
            oldest = compositor->cache[index].last_used;
            victim = index;
        }
    if (victim == ZD_MAX_SCENE_NODES)
        return;
    compositor->cache_bytes -= compositor->cache[victim].bytes;
    compositor->cache[victim].valid = 0;
    ++compositor->stats.cache_evictions;
}

static void cache_touch(struct zd_compositor *compositor, zd_node_id owner,
                        uint32_t resource_key, uint64_t bytes) {
    uint32_t index;
    uint32_t slot = ZD_MAX_SCENE_NODES;
    uint64_t now = compositor->current_frame;

    for (index = 0; index < ZD_MAX_SCENE_NODES; ++index) {
        if (!compositor->cache[index].valid) {
            if (slot == ZD_MAX_SCENE_NODES)
                slot = index;
            continue;
        }
        if (compositor->cache[index].owner == owner) {
            if (compositor->cache[index].resource_key != resource_key) {
                compositor->cache_bytes -= compositor->cache[index].bytes;
                compositor->cache[index].bytes = bytes;
                compositor->cache_bytes += bytes;
                compositor->cache[index].resource_key = resource_key;
            }
            compositor->cache[index].last_used = now;
            ++compositor->stats.cache_hits;
            return;
        }
    }

    ++compositor->stats.cache_misses;
    while (compositor->cache_bytes + bytes > compositor->cache_budget_bytes) {
        uint64_t valid_count = 0;
        for (index = 0; index < ZD_MAX_SCENE_NODES; ++index)
            if (compositor->cache[index].valid)
                ++valid_count;
        if (valid_count == 0)
            break;
        cache_evict_lru(compositor);
    }
    if (compositor->cache_bytes + bytes > compositor->cache_budget_bytes)
        return;
    if (slot == ZD_MAX_SCENE_NODES)
        return;
    compositor->cache[slot].valid = 1;
    compositor->cache[slot].owner = owner;
    compositor->cache[slot].resource_key = resource_key;
    compositor->cache[slot].bytes = bytes;
    compositor->cache[slot].last_used = now;
    compositor->cache_bytes += bytes;
}

/* Software raster ------------------------------------------------------ */

static void fill_rect(struct zd_bitmap target, struct zd_rect rect,
                      uint32_t color, uint64_t *written) {
    int32_t x0 = rect.x < 0 ? 0 : rect.x;
    int32_t y0 = rect.y < 0 ? 0 : rect.y;
    int32_t x1 = rect.x + rect.w > target.width ? target.width :
                 rect.x + rect.w;
    int32_t y1 = rect.y + rect.h > target.height ? target.height :
                 rect.y + rect.h;
    int32_t y;
    if (x1 <= x0 || y1 <= y0)
        return;
    for (y = y0; y < y1; ++y) {
        uint32_t *row = target.pixels + (int64_t)y * target.stride_px;
        int32_t x;
        for (x = x0; x < x1; ++x)
            row[x] = color;
    }
    if (written)
        *written += (uint64_t)(x1 - x0) * (uint64_t)(y1 - y0);
}

static void blit_rect(struct zd_bitmap target, struct zd_rect dst_rect,
                      const uint32_t *source, int32_t source_width,
                      int32_t source_height, int32_t source_stride,
                      struct zd_rect damage, uint64_t *written) {
    struct zd_rect clip = zd_rect_intersection(dst_rect, damage);
    clip = zd_rect_intersection(clip, (struct zd_rect){
        0, 0, target.width, target.height
    });
    if (zd_rect_empty(clip) || !source)
        return;
    {
        int32_t y;
        for (y = clip.y; y < clip.y + clip.h; ++y) {
            int32_t src_y = y - dst_rect.y;
            uint32_t *row;
            int32_t x;
            if (src_y < 0 || src_y >= source_height)
                continue;
            row = target.pixels + (int64_t)y * target.stride_px;
            for (x = clip.x; x < clip.x + clip.w; ++x) {
                int32_t src_x = x - dst_rect.x;
                if (src_x < 0 || src_x >= source_width)
                    continue;
                row[x] = source[(int64_t)src_y * source_stride + src_x];
                if (written)
                    ++(*written);
            }
        }
    }
}

int zd_compositor_present(struct zd_compositor *compositor,
                          struct zd_bitmap target, uint64_t now_ns) {
    uint32_t damage_index;
    uint32_t node_index;
    uint32_t listener;
    uint64_t pixels_written = 0;
    struct zd_rect damaged_area = {0, 0, 0, 0};

    if (!compositor || !target.pixels || target.width <= 0 ||
        target.height <= 0)
        return -ZD_EINVAL;
    if (compositor->damage_count == 0) {
        ++compositor->stats.frames_skipped_not_dirty;
        return 0;
    }
    if (now_ns < compositor->next_present_ns) {
        ++compositor->stats.frames_skipped_paced;
        return -ZD_EAGAIN;
    }
    if (!compositor->scene_valid)
        zd_compositor_sync_scene(compositor);

    /* 1. Background fill strictly inside each damage rect (attributed
     *    desktop damage covers regions no window claimed). */
    for (damage_index = 0; damage_index < compositor->damage_count;
         ++damage_index)
        fill_rect(target, compositor->damage[damage_index].rect, 0xFF101820U,
                  &pixels_written);

    /* 2. Composite nodes bottom -> top, only inside damage rects. */
    for (node_index = 0; node_index < compositor->node_count; ++node_index) {
        const struct zd_scene_node *node = &compositor->nodes[node_index];
        const uint32_t *source = (const uint32_t *)0;
        int32_t source_width = 0;
        int32_t source_height = 0;
        int32_t source_stride = 0;
        if (!node->enabled)
            continue;
        if (node->is_window && compositor->pixel_source)
            source = compositor->pixel_source(compositor->pixel_context,
                                              node->id, &source_width,
                                              &source_height, &source_stride);
        for (damage_index = 0; damage_index < compositor->damage_count;
             ++damage_index) {
            struct zd_rect clip = compositor->damage[damage_index].rect;
            if (!zd_rect_intersects(clip, node->physical))
                continue;
            if (source) {
                blit_rect(target, node->physical, source, source_width,
                          source_height, source_stride, clip,
                          &pixels_written);
                cache_touch(compositor, node->id, node->resource_key,
                            (uint64_t)source_width * (uint64_t)source_height *
                            4ULL);
            } else {
                struct zd_rect clipped =
                    zd_rect_intersection(node->physical, clip);
                fill_rect(target, clipped, 0xFF2A6FDBU, &pixels_written);
            }
        }
    }

    /* Bounding box for stats/listeners. */
    {
        int32_t x0 = 0x7fffffff, y0 = 0x7fffffff;
        int32_t x1 = (-0x7fffffff - 1), y1 = (-0x7fffffff - 1);
        for (damage_index = 0; damage_index < compositor->damage_count;
             ++damage_index) {
            struct zd_rect rect = compositor->damage[damage_index].rect;
            if (rect.x < x0)
                x0 = rect.x;
            if (rect.y < y0)
                y0 = rect.y;
            if (rect.x + rect.w > x1)
                x1 = rect.x + rect.w;
            if (rect.y + rect.h > y1)
                y1 = rect.y + rect.h;
            compositor->stats.damage_pixels_visible += zd_rect_area(rect);
        }
        damaged_area.x = x0;
        damaged_area.y = y0;
        damaged_area.w = x1 - x0;
        damaged_area.h = y1 - y0;
    }

    compositor->stats.pixels_written += pixels_written;
    ++compositor->stats.frames_presented;
    ++compositor->current_frame;
    compositor->damage_count = 0;
    compositor->damage_pixels_pending = 0;
    compositor->next_present_ns = now_ns + compositor->frame_interval_ns;

    for (listener = 0; listener < compositor->listener_count; ++listener)
        compositor->listeners[listener].on_present(
            compositor->listeners[listener].context, compositor->current_frame,
            damaged_area);
    return (int)zd_min_u32((uint32_t)pixels_written, 0x7fffffffU);
}

void zd_compositor_drop_client(struct zd_compositor *compositor,
                               zd_client_id client) {
    uint32_t index;
    if (!compositor || !compositor->wm || client == 0)
        return;
    for (index = 0; index < ZD_MAX_WINDOWS; ++index) {
        const struct zd_window *window = &compositor->wm->windows[index];
        uint32_t slot;
        if (!window->in_use || window->client != client)
            continue;
        for (slot = 0; slot < ZD_MAX_SCENE_NODES; ++slot)
            if (compositor->cache[slot].valid &&
                compositor->cache[slot].owner == window->id) {
                compositor->cache_bytes -= compositor->cache[slot].bytes;
                compositor->cache[slot].valid = 0;
            }
        /* Drop pending damage attributed to the dead client. */
        {
            uint32_t damage_index = 0;
            while (damage_index < compositor->damage_count) {
                if (compositor->damage[damage_index].owner == window->id) {
                    for (; damage_index + 1U < compositor->damage_count;
                         ++damage_index)
                        compositor->damage[damage_index] =
                            compositor->damage[damage_index + 1U];
                    --compositor->damage_count;
                    continue;
                }
                ++damage_index;
            }
        }
    }
    compositor->scene_valid = 0;
}

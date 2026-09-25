#include <zeroos/desktop/display.h>
#include <zeroos/desktop/metrics.h>

#define ZD_DISPLAY_DEFAULT_FAILURE_LIMIT 3U

int zd_display_service_init(struct zd_display_service *service,
                            const struct zd_display_ops *ops,
                            uint32_t failure_limit) {
    uint32_t i;

    if (!service || !ops || !ops->query_info || !ops->present || !ops->ticks)
        return -ZD_EINVAL;
    service->ops = *ops;
    for (i = 0; i < sizeof(service->info); ++i)
        ((uint8_t *)&service->info)[i] = 0;
    service->state = ZD_DISPLAY_ATTACHING;
    service->width = 0;
    service->height = 0;
    service->metrics = 0;
    service->min_present_interval_ticks = 0;
    service->last_present_tick = 0;
    service->has_presented = 0;
    service->consecutive_failures = 0;
    service->failure_limit =
        failure_limit ? failure_limit : ZD_DISPLAY_DEFAULT_FAILURE_LIMIT;
    service->pending_damage.x = 0;
    service->pending_damage.y = 0;
    service->pending_damage.w = 0;
    service->pending_damage.h = 0;
    service->pending_valid = 0;
    for (i = 0; i < sizeof(service->stats); ++i)
        ((uint8_t *)&service->stats)[i] = 0;
    return 0;
}

static int geometry_valid(const struct zeroos_display_info *info) {
    uint64_t min_pitch;

    if (!info->width || !info->height)
        return 0;
    if (info->bpp != 24 && info->bpp != 32)
        return 0;
    min_pitch = ((uint64_t)info->width * info->bpp) / 8U;
    if ((uint64_t)info->pitch < min_pitch)
        return 0;
    if ((uint64_t)info->pitch * info->height > info->byte_size)
        return 0;
    return 1;
}

int zd_display_service_attach(struct zd_display_service *service) {
    struct zeroos_display_info info;
    int result;
    uint32_t i;

    if (!service)
        return -ZD_EINVAL;
    for (i = 0; i < sizeof(info); ++i)
        ((uint8_t *)&info)[i] = 0;
    result = service->ops.query_info(service->ops.context, &info);
    if (result != 0) {
        service->state = ZD_DISPLAY_DEGRADED;
        service->width = 0;
        service->height = 0;
        service->pending_valid = 0;
        return result;
    }
    if (!geometry_valid(&info)) {
        service->state = ZD_DISPLAY_DEGRADED;
        service->width = 0;
        service->height = 0;
        service->pending_valid = 0;
        return -ZD_EINVAL;
    }
    service->info = info;
    service->width = info.width;
    service->height = info.height;
    service->pending_valid = 0;
    service->consecutive_failures = 0;
    if (!(info.flags & ZEROOS_DISPLAY_FLAG_PRESENT)) {
        service->state = ZD_DISPLAY_DEGRADED;
        return -ZD_ENOENT;
    }
    service->state = ZD_DISPLAY_LIVE;
    return 0;
}

int zd_display_service_damage(struct zd_display_service *service,
                              struct zd_rect rect) {
    int64_t x0, y0, x1, y1;

    if (!service || !service->width || !service->height)
        return -ZD_EINVAL;

    x0 = rect.x;
    y0 = rect.y;
    x1 = rect.x + rect.w;
    y1 = rect.y + rect.h;
    if (rect.w <= 0 || rect.h <= 0)
        return 0;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > (int64_t)service->width)
        x1 = service->width;
    if (y1 > (int64_t)service->height)
        y1 = service->height;
    if (x0 >= x1 || y0 >= y1)
        return 0; /* fully outside the scanout */

    if (!service->pending_valid) {
        service->pending_damage.x = (int32_t)x0;
        service->pending_damage.y = (int32_t)y0;
        service->pending_damage.w = (int32_t)(x1 - x0);
        service->pending_damage.h = (int32_t)(y1 - y0);
        service->pending_valid = 1;
    } else {
        int64_t px0 = service->pending_damage.x;
        int64_t py0 = service->pending_damage.y;
        int64_t px1 = px0 + service->pending_damage.w;
        int64_t py1 = py0 + service->pending_damage.h;
        if (x0 < px0)
            px0 = x0;
        if (y0 < py0)
            py0 = y0;
        if (x1 > px1)
            px1 = x1;
        if (y1 > py1)
            py1 = y1;
        service->pending_damage.x = (int32_t)px0;
        service->pending_damage.y = (int32_t)py0;
        service->pending_damage.w = (int32_t)(px1 - px0);
        service->pending_damage.h = (int32_t)(py1 - py0);
    }
    service->stats.damage_rects_accepted++;
    return 0;
}

int zd_display_service_present(struct zd_display_service *service,
                               const void *frame_base,
                               uint32_t stride_bytes, uint64_t now_tick) {
    uint32_t x, y, w, h;
    uint32_t bytes_pp;
    const uint8_t *rect_ptr;
    int result;

    if (!service || !frame_base)
        return -ZD_EINVAL;

    if (service->state == ZD_DISPLAY_DEGRADED) {
        service->stats.presents_refused_degraded++;
        zd_metrics_add(service->metrics, ZD_METRIC_FRAMES_REFUSED, 1);
        return -ZD_ENOENT;
    }
    if (service->state == ZD_DISPLAY_SUSPENDED) {
        service->stats.presents_refused_suspended++;
        zd_metrics_add(service->metrics, ZD_METRIC_FRAMES_REFUSED, 1);
        return -ZD_ESTATE;
    }
    if (service->state != ZD_DISPLAY_LIVE)
        return -ZD_ESTATE;
    if (!service->pending_valid) {
        service->stats.presents_refused_empty++;
        zd_metrics_add(service->metrics, ZD_METRIC_FRAMES_REFUSED, 1);
        return -ZD_EAGAIN;
    }
    if (service->min_present_interval_ticks && service->has_presented) {
        uint64_t elapsed = now_tick - service->last_present_tick;
        if (elapsed < service->min_present_interval_ticks) {
            service->stats.presents_refused_paced++;
            zd_metrics_add(service->metrics, ZD_METRIC_FRAMES_REFUSED, 1);
            return -ZD_EAGAIN;
        }
    }

    x = (uint32_t)service->pending_damage.x;
    y = (uint32_t)service->pending_damage.y;
    w = (uint32_t)service->pending_damage.w;
    h = (uint32_t)service->pending_damage.h;
    bytes_pp = service->info.bpp / 8U;
    rect_ptr = (const uint8_t *)frame_base +
               (uint64_t)y * stride_bytes + (uint64_t)x * bytes_pp;

    result = service->ops.present(service->ops.context, x, y, w, h,
                                  stride_bytes, rect_ptr);
    if (result != 0) {
        service->stats.present_failures++;
        zd_metrics_add(service->metrics, ZD_METRIC_PRESENT_FAILURES, 1);
        service->consecutive_failures++;
        if (service->consecutive_failures >= service->failure_limit) {
            service->state = ZD_DISPLAY_SUSPENDED;
            service->consecutive_failures = 0;
        }
        return result;
    }

    service->stats.frames_presented++;
    service->stats.pixels_submitted += (uint64_t)w * h;
    zd_metrics_add(service->metrics, ZD_METRIC_FRAMES_PRESENTED, 1);
    if (service->has_presented)
        zd_metrics_record_interval(service->metrics,
                                   now_tick - service->last_present_tick);
    service->last_present_tick = now_tick;
    service->has_presented = 1;
    service->consecutive_failures = 0;
    service->pending_valid = 0;
    service->pending_damage.w = 0;
    service->pending_damage.h = 0;
    return 0;
}

/* Attach a part-L metrics recorder (optional; NULL detaches). */
void zd_display_service_set_metrics(struct zd_display_service *service,
                                    struct zd_metrics *metrics) {
    if (service)
        service->metrics = metrics;
}

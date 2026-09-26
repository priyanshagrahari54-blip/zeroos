#ifndef ZEROOS_DESKTOP_DESKTOP_H
#define ZEROOS_DESKTOP_DESKTOP_H

/* Umbrella header for the ZEROOS desktop platform core.
 *
 * Architecture (Stage 5, Part A):
 *   kernel primitives (display/MMIO, timer vsync, input IRQs)
 *     -> display service (owns scanout; boundary pending on-target FB map)
 *     -> GPU abstraction (software raster now; accel interface later)
 *     -> compositor (this core: scene/damage/pacing/present)
 *     -> window system (this core: focus/stacking/workspaces/routing)
 *     -> shell + application UI (consumes this core over its listeners)
 *
 * The core is desktop policy only. It contains no kernel code, no polling
 * loops and no unbounded allocation. See docs/GRAPHICS_DESKTOP.md. */

#include <zeroos/desktop/common.h>
#include <zeroos/desktop/lifecycle.h>
#include <zeroos/desktop/governor.h>
#include <zeroos/desktop/window.h>
#include <zeroos/desktop/input.h>
#include <zeroos/desktop/compositor.h>
#include <zeroos/desktop/display.h>
#include <zeroos/desktop/automation.h>
#include <zeroos/desktop/browser.h>
#include <zeroos/desktop/ai.h>
#include <zeroos/desktop/bar.h>
#include <zeroos/desktop/launcher.h>
#include <zeroos/desktop/capability.h>
#include <zeroos/desktop/metrics.h>
#include <zeroos/desktop/update.h>
#include <zeroos/desktop/url.h>
#include <zeroos/desktop/study.h>
#include <zeroos/desktop/fps.h>
#include <zeroos/desktop/snapshot.h>
#include <zeroos/desktop/vault.h>
#include <zeroos/desktop/nav.h>
#include <zeroos/desktop/firewall.h>
#include <zeroos/desktop/sandbox.h>
#include <zeroos/desktop/clipboard.h>
#include <zeroos/desktop/downloads.h>
#include <zeroos/desktop/providers.h>
#include <zeroos/desktop/perfcenter.h>
#include <zeroos/desktop/pdf.h>
#include <zeroos/desktop/media.h>
#include <zeroos/desktop/gaming.h>
#include <zeroos/desktop/search.h>
#include <zeroos/desktop/settings.h>
#include <zeroos/desktop/notify.h>
#include <zeroos/desktop/a11y.h>
#include <zeroos/desktop/i18n.h>
#include <zeroos/desktop/watchdog.h>

#endif

# ZEROOS — ZERO-RENDER IDLE ARCHITECTURE

Status: ARCHITECTURE / PERFORMANCE SPECIFICATION

## 1. Goal

ZEROOS must make a static UI remain visible without continuously rendering it.

Target:
`UI visible + unchanged -> 0 application redraw + 0 compositor redraw + 0 unnecessary GPU composition`

This means zero unnecessary software/GPU rendering work, not literally zero electrical display power or zero framebuffer memory. The panel still consumes power while it is on.

## 2. Core Architecture

Use a retained-surface, damage-driven compositor instead of a permanent render loop.

When the scene is unchanged:
1. retain the last valid composed buffer;
2. stop application frame production;
3. stop compositor composition;
4. stop synthetic frame requests where hardware permits;
5. let the display scan out the existing buffer;
6. wake only on a real visual-state change.

The architecture follows proven compositor ideas such as damage tracking, frame callbacks and compositor sleep/offscreen states.

## 3. Render State Machine

```text
STATIC -> DIRTY -> RENDER -> COMPOSE -> PRESENT -> STATIC
```

Animation:

```text
ANIMATION_REQUEST -> ACTIVE_ANIMATION -> FRAME_CALLBACK/VSYNC
-> RENDER -> PRESENT -> next frame only if still needed -> STATIC
```

There is never an unconditional `while(true) render()` loop.

## 4. Static Frame Retention

After presentation, retain the resulting surface/buffer. If nothing changes, do not rebuild it.

Required mechanisms:
- retained scene graph;
- explicit damage regions;
- surface/buffer reuse;
- cached text/icon assets;
- direct scanout when possible;
- hardware overlay planes when available;
- no full-screen redraw for local changes.

## 5. Damage-Only Rendering

Every visual change declares its affected region.

Examples:
- one typed character -> affected text region;
- notification badge -> badge region;
- moved window -> impacted regions;
- unchanged wallpaper -> no redraw;
- closed Dynamic Capsule -> no Capsule render;
- unchanged secondary monitor -> no recomposition.

Full-screen composition is only allowed when technically required.

## 6. Cheapest Presentation Path

Use this priority where hardware supports it:

```text
Direct Scanout
   > Hardware Overlay
   > Cached Composition
   > Partial GPU Composition
   > Full GPU Composition
   > Software Fallback
```

Fullscreen video/app surfaces should be candidates for direct scanout. Cursor and suitable UI layers should use hardware planes where possible.

## 7. Glass With No Continuous Blur

Glass is a visual material, not a permanent blur process.

For a static glass surface:
1. sample the required backdrop only when it changes;
2. calculate the glass result once;
3. cache it;
4. reuse it while the backdrop is unchanged.

Never run a full-screen blur every refresh interval.

Fallback:
`Glass -> Soft -> Solid`

The fallback preserves layout and interaction while reducing GPU cost.

## 8. Dynamic Capsule

Idle state:
- no animation timer;
- no polling loop;
- no continuous render;
- minimal controller state.

Event:

```text
Event
 -> wake controller
 -> update state
 -> request required frame(s)
 -> animate only if enabled
 -> settle/cache
 -> return to idle
```

Wake sources include media changes, downloads, timers, calls, recording, ZERO AI tasks and update/recovery events. Progress updates must be rate-limited.

## 9. Animation Is Optional

Modes:
- **Off:** immediate transitions; zero animation frames.
- **Minimal:** essential transitions only.
- **Standard:** normal short transitions.
- **Expressive:** premium motion when policy permits.

When enabled, render only on the display's pacing callback/VSYNC. Do not render faster than the display can present.

## 10. Idle Scheduler

When all surfaces are clean:
- no compositor render timer;
- no animation timer;
- no wallpaper processing loop;
- no continuous glass recomputation;
- no Dynamic Capsule polling;
- no periodic UI render loop.

Wake only from real events: input, surface damage, window state change, notification, media state, display configuration, timer deadline or system state change.

## 11. Refresh-Rate / Display Idle

Where supported:
- active UI -> adaptive/normal refresh;
- static UI -> lowest suitable refresh or display idle mode;
- animation -> restore required refresh;
- video -> media-synchronized cadence;
- gaming/presentation -> performance policy.

Do not force maximum refresh rate for a static desktop.

## 12. Wallpaper and Ambient Effects

Static wallpaper is loaded, decoded and cached once. Theme colors are derived from a small palette sample once. Never continuously analyze the full screen.

Live wallpaper is an explicitly active feature and may consume resources.

## 13. Multi-Monitor

Each display has independent presentation state. If display A changes and display B does not, only A is invalidated/recomposed.

## 14. Tablet / Touch

Touch does not imply continuous rendering. Gesture previews render only while the gesture is active; after release the final state is cached and the compositor returns to idle.

## 15. Resource Contract

For an unchanged desktop scene, the target is:
- application redraw: **0/s**;
- compositor redraw: **0/s**;
- animation frames: **0/s** when animation is Off;
- unnecessary GPU composition: **0**;
- UI polling: **0**;
- frame generation: **0** until a visual event requires it.

Retained framebuffer memory and physical display power are explicitly excluded from the "zero render" claim.

## 16. Failure/Fallback

If an optimization fails:
1. invalidate only the affected surface;
2. use cached/partial composition;
3. use software composition only when necessary;
4. record the reason;
5. never recover by entering a busy render loop.

## 17. Verification Requirements

Production implementation must demonstrate:
- 60-second unchanged desktop: zero application redraw callbacks;
- zero unnecessary compositor cycles during that period;
- opening/closing a menu returns to zero-render idle;
- animation Off produces no animation frames;
- animation On stops immediately after completion;
- Dynamic Capsule wakes only for relevant events;
- static glass does not continuously recompute blur;
- unchanged secondary monitors do not recompose;
- eligible fullscreen surfaces use direct scanout/overlay when supported;
- CPU/GPU counters show no hidden periodic render workload.

## 18. Acceptance Rule

Low average CPU usage is not sufficient. ZEROOS must prove a true clean/idle presentation path where unchanged visual state requires no new software/GPU frame generation.

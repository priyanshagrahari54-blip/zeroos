# ZEROOS — VISUAL DESIGN, THEMES, ADAPTIVE UI & APP SUITE

Status: DESIGN / PRODUCT SPECIFICATION
This document defines the intended ZEROOS visual system. It does not claim that the described UI or apps are already implemented.

## 1. Design Direction

ZEROOS is an original desktop-first OS that can borrow familiar usability patterns without copying Windows, ChromeOS, macOS or Android.

Target character:
- premium but lightweight;
- glass-like but not blur-dependent;
- rounded, calm and spatial;
- expressive color grading;
- fast transitions;
- touch-friendly on tablets;
- dense and keyboard-efficient on desktop;
- graceful on low-end hardware.

Windows 12 is treated only as a visual reference category, not a dependency or cloning target. ZEROOS uses its own component language.

## 2. ZERO Material System

### 2.1 Materials

Four surface levels:
- Solid: lowest GPU cost and highest readability.
- Soft: subtle translucency without expensive continuous blur.
- Glass: translucent surface with bounded, event-driven backdrop sampling.
- Hero: premium visual treatment reserved for focused/important surfaces.

Blur is optional and budget-aware. A glass surface always has a solid fallback.

### 2.2 Geometry

Global geometry uses a coherent radius scale:
- 6px micro controls;
- 10px controls;
- 14px cards;
- 18px windows;
- 24px hero surfaces;
- pill geometry for compact controls and Dynamic Capsule states.

Touch targets are larger on tablet mode while design tokens remain shared.

### 2.3 Color Grading

Themes use background, elevated surface, text hierarchy, accent, success, warning and error tokens. Accent colors may be derived from wallpaper or selected manually.

Color is never the sole carrier of status.

### 2.4 Lighting

Optional ambient lighting derives from a small sampled palette. It never continuously analyzes the whole screen.

## 3. ZERO Themes

### Aurora
Dark navy base, cyan/indigo accent, soft aurora gradients. Default premium theme.

For the Lenovo G560 reference profile, the default preset is **Aurora Legacy**: Aurora colors with lower animation, bounded glass, 1366x768 density tuning and HDD/thermal-aware effects.

### Obsidian Glass
Near-black surfaces, restrained white text, electric blue accent. Power-user oriented.

### Pearl
Light neutral surfaces, subtle glass, blue-violet accent. Productivity oriented.

### Solar
Warm cream/orange highlights with strong readability.

### Ocean
Deep blue with teal/cyan accents and cool glass layers.

### Forest
Dark green/emerald accents for calm working environments.

### Sunset
Purple, magenta and warm orange accents with adaptive wallpaper grading.

### Mono
Near-monochrome, almost no transparency or decorative animation. Accessibility and battery oriented.

### High Contrast
Strict contrast ratios, solid surfaces, minimal transparency and explicit focus rings.

### Custom Theme Studio
Users can configure wallpaper, accent, material strength, corner radius, density, icon style, animation level, transparency and sound profile. Every custom theme has a preview and safe fallback.

## 4. Desktop Layout

### ZERO Bar
Primary adaptive shell containing launcher, Universal Search, workspace, active app/window context, Dynamic Capsule, network/audio/battery status, notifications and profile/system menu.

### Dynamic Capsule
A small contextual surface that expands only for active events: music, downloads, recording, calls, timers, navigation, AI tasks and update/recovery.

Idle state is tiny and has **no continuous render loop**. Event state requests only the frames needed to show the change, then returns to the cached idle state.

### Floating Dock
Adaptive-width dock with relevant and pinned apps. Can become a side dock or compact tablet shelf.

## 5. Window System

Windows support rounded corners, snap zones, resize, maximize/minimize, workspace movement, always-on-top, picture-in-picture where applicable, edge gestures, keyboard management and smooth open/close transitions.

Animations are interruptible and state-driven. Low-power mode can use immediate transitions.

## 6. Tablet / 2-in-1 Mode

ZEROOS is not simply a scaled desktop.

Touch-first mode provides larger targets, gesture-friendly shelf, edge gestures, split-screen/multi-window layouts, touch-expandable Dynamic Capsule, keyboard shortcuts when connected, stylus/input APIs and integrated virtual keyboard.

Desktop -> Hybrid -> Tablet is capability/state based and preserves running application/workspace state without restarting the shell.

## 7. Large Display / UHD Mode

UHD/high-DPI uses vector-first icons, DPI-aware text, fractional scaling, density adaptation, high-resolution wallpapers, HDR-aware media where supported, multi-monitor topology and per-display scaling/refresh rate.

4K resolution does not imply permanently loaded 4K textures; assets are resolution-aware and cached within bounds.

## 8. Motion System

Motion levels:
- Instant;
- Minimal;
- Standard;
- Expressive.

**Instant/Off is a first-class mode, not a fallback.** With animation disabled, state changes are committed directly and no animation frame loop runs.

Expressive animation is disabled automatically when thermal/power/resource policy requires it.

## 9. ZERO-Render Idle Rule

The UI must remain visible without continuously rendering it.

For an unchanged scene:

```text
VISIBLE + UNCHANGED
       |
       v
retain last composed buffer
       |
       v
NO APP REDRAW
NO COMPOSITOR REDRAW
NO GPU COMPOSITION
       |
       v
wait for visual event
```

ZEROOS uses a retained-surface, damage-driven compositor. Once a frame is presented, it remains reusable until a visual state changes. The display can scan out the existing buffer without requiring a new software/GPU frame.

Required mechanisms:
- retained scene graph;
- damage-region tracking;
- surface/buffer reuse;
- direct scanout when possible;
- hardware overlay planes where available;
- event-driven frame scheduling;
- no unconditional render loop;
- independent per-monitor invalidation;
- cached glass/material results.

### Glass without continuous rendering

A glass surface is sampled/computed only when its backdrop changes. The result is cached and reused. No full-screen blur runs every refresh interval.

Fallback:
`Glass -> Soft -> Solid`

### Dynamic Capsule idle behavior

```text
IDLE -> no timer / no polling / no render
EVENT -> wake -> update -> required frame(s) -> cache -> IDLE
```

### Animation behavior

Animation Off means **zero animation frames**. When animation is enabled, each next frame is requested only after the display pacing callback/VSYNC and only while the animation remains active.

### Resource target

For an unchanged desktop scene:
- application redraw: 0/s;
- compositor redraw: 0/s;
- animation frames: 0/s when Off;
- unnecessary GPU composition: 0;
- UI polling: 0;
- new frame generation: 0 until a visual event occurs.

This does not claim zero physical display power or zero retained framebuffer memory.

Detailed engineering requirements are defined in [`ZEROOS_ZERO_RENDER_IDLE_ARCHITECTURE.md`](./ZEROOS_ZERO_RENDER_IDLE_ARCHITECTURE.md).

## 10. Premium Native App Suite

### ZERO Browser
Tabs/groups, vertical tabs, reader mode, downloads, history, profiles, privacy, site permissions, PiP, resource-aware tab lifecycle and isolated renderer processes.

### ZERO Files
Dual-pane, tabs, quick drop, transfer queue, preview, archive support, storage health, permissions, recent/semantic search and safe delete/recovery.

### ZERO Music
Local library, playlists, queue, equalizer, gapless playback where supported, lyrics when legally/technically available, hardware audio path, optional external-service connectors and offline-first local playback.

### ZERO Video
VLC-class architecture with broad legal/available codec support, hardware decoding, subtitles, audio tracks, speed control, frame stepping, screenshots, playlists, network streams, HDR where supported and low-resource software fallback.

### ZERO Terminal
Tabs, split panes, searchable scrollback, profiles, command palette, SSH/local shell integration, safe elevation and GPU rendering only when beneficial.

### ZERO Study
PDF/ebook reading, notes, OCR, flashcards, focus timer, dictionary, formula tools, citation workspace, offline library and optional ZERO AI explanation.

### ZERO Studio
Screenshot, screen recording, annotation, basic video trimming, compression/export, capture history and hardware encoder when available.

### ZERO Control
Unified controls for Wi-Fi, Bluetooth, display, audio, battery, performance profile, firewall, privacy, devices, notifications and accessibility.

### ZERO Health
CPU/RAM/GPU, storage, thermal, battery, driver, network, crash and update/recovery readiness dashboard.

### ZERO AI
Chat, file understanding, search, diagnostics, controlled automation, coding, study and media actions. Every privileged action uses the ZERO AI permission broker.

## 11. Resource Contract

Premium appearance must not require premium hardware.

Mechanisms:
- no permanent full-screen blur pass;
- bounded backdrop sampling;
- cached/vector assets;
- lazy icon loading;
- event-driven Dynamic Capsule;
- dormant widget controllers;
- demand-loaded AI/browser/media engines;
- adaptive animation;
- display-rate-aware frame scheduling;
- surface reuse;
- damage-only composition;
- direct-scanout/overlay fast paths;
- automatic effects reduction under memory/GPU/thermal pressure.

“0 resource” means near-zero unnecessary idle overhead. Active video, AI, animation or other active work necessarily consumes resources.

## 12. G560 Reference Preset

The Lenovo G560 uses the Aurora Legacy preset by default. It targets the 15.6-inch 1366x768 display and legacy Intel/NVIDIA graphics variants with compact geometry, Minimal motion, bounded caches, HDD-aware loading, thermal-aware effects, eye-protection controls and keyboard-first multitasking. See `ZEROOS_LENOVO_G560_REFERENCE_PROFILE.md` for the complete hardware/UI contract.

## 13. Theme Performance Profiles

Every theme declares visual cost, blur, animation, texture and idle-controller budgets plus a low-power fallback.

Themes are presentation configurations, not hidden background services.

## 14. App Design Contract

Every first-party app provides shared design tokens, light/dark/high-contrast variants, keyboard navigation, touch layout, offline/permission/low-resource/reduced-motion states, empty/loading/error states, searchable commands, context menus, deep links, state restoration and crash-safe recovery.

Apps must also implement the zero-render idle contract: unchanged views remain retained and do not continuously repaint.

## 15. Accessibility and Readability

Glass never reduces text readability. The compositor can increase opacity, disable background motion or switch to solid surfaces when accessibility requires it.

## 16. Originality Rule

ZEROOS may combine familiar desktop, mobile and tablet interaction ideas, but its branding, icons, component geometry, information architecture and interaction grammar remain original.

## 17. Acceptance Checklist

A theme/app experience is accepted only when:
- desktop and tablet layouts are validated;
- low-resource fallback exists;
- reduced-motion works;
- animation Off produces no animation loop;
- high-contrast works;
- keyboard and touch flows work;
- no unnecessary background polling exists;
- unchanged views demonstrate zero redraw activity;
- resource cost is measured;
- startup/wakeup latency is measured;
- crash/recovery state exists;
- visual regression tests exist;
- implementation status is separate from design completion.


## 18. Input, Touchpad, Clipboard, Capture & Shortcut Contract

ZEROOS treats keyboard, touchpad, mouse, touchscreen and stylus input as first-class system input. Defaults are configurable and capability-aware.

### 18.1 Clipboard & Text
- Ctrl+C / Ctrl+X / Ctrl+V: copy / cut / paste
- Ctrl+Shift+V: paste without formatting where supported
- Ctrl+A: select all; Ctrl+Z / Ctrl+Y: undo / redo
- Ctrl+F / Ctrl+H: find / replace where supported
- Ctrl+K: command/search palette where supported
- Shift+Insert / Ctrl+Insert: paste / copy
- Delete / Shift+Delete: delete / permanent-delete when permitted
- configurable clipboard-history shortcut; sensitive clipboard data can be excluded and sync is opt-in.

### 18.2 Screenshots & Recording
- PrtSc: full-screen capture
- Alt+PrtSc: focused-window capture
- Shift+PrtSc: region capture
- configurable screenshot editor and screen-record start/stop shortcuts
- Dynamic Capsule shows capture/recording state only while active.

### 18.3 Window & Workspace
- Alt+Tab / Alt+Shift+Tab: switch / reverse switch
- Alt+F4: close
- Win+D: desktop
- Win+Tab: overview
- Win+Left/Right: snap
- Win+Up/Down: maximize/restore/minimize
- Win+Shift+Left/Right: move window/display where supported
- Win+Ctrl+Left/Right: switch workspace
- Win+Ctrl+D / Win+Ctrl+F4: create / close workspace
- Win+M / Win+Shift+M: minimize / restore
- F11: fullscreen where supported
- Esc: dismiss transient UI / exit current mode where safe.

### 18.4 System & Accessibility
- Win: launcher; Win+Space: input language/layout
- Win+A: quick controls; Win+N: notifications
- Win+S: Universal Search; Win+I: ZERO Control
- Win+E: ZERO Files; Win+T: Terminal
- Win+B: status area; Win+L: lock session
- Ctrl+Alt+Delete: secure system/session screen
- dedicated configurable accessibility shortcuts for high contrast, screen reader, magnifier, keyboard navigation and reduced motion.

### 18.5 Touchpad & Mouse
- one-finger move: pointer; tap/left click: primary click
- two-finger tap: secondary click
- two-finger drag: scroll; pinch: zoom where supported
- two-finger swipe: navigation where supported
- three-finger left/right: workspace/app switching
- three-finger up: overview; three-finger down: desktop/minimize where supported
- four-finger gestures: configurable workspace/media/system actions
- optional two-finger rotate where the application declares support
- settings expose tap-to-click, natural/traditional scrolling, pointer speed, acceleration, palm rejection, button mapping and per-device profiles.

### 18.6 Touchscreen & Stylus
Tap, press-and-hold context action, drag/select, pinch zoom, two-finger pan/scroll and capability-aware edge gestures. Stylus pressure/tilt APIs are exposed when hardware supports them. Every core gesture has a keyboard/mouse equivalent.

### 18.7 Universal Command Palette
A searchable command palette is available from a configurable shortcut and exposes apps, actions, settings, window operations, media controls and accessibility commands. Commands are permission-aware and context-sensitive.

### 18.8 Shortcut Safety
Shortcuts are conflict-checked. Security/recovery shortcuts cannot be overridden. Users can remap, export/import profiles and restore defaults. App shortcuts are scoped so they cannot steal global system controls unexpectedly.

## 19. Micro-feedback, Emoji & Convenience

ZEROOS provides bounded event-driven feedback for copy/paste, screenshot, download, Bluetooth connection, volume, brightness, workspace switching, snapping, recording, file operations and security prompts. Emoji are first-class Unicode text/icons with fallback font coverage; the emoji picker is searchable and keyboard-accessible. No decorative feedback creates a permanent polling/render loop.

## 20. App Interaction Contract Addendum

First-party apps expose copy/cut/paste, undo/redo, find, context menus, command palette and keyboard navigation where applicable. Touch/long-press alternatives exist for core pointer actions. Interactive state is accessible without relying on color alone.

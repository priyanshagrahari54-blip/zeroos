# ZEROOS — UI/UX DESIGN SYSTEM
Version: 2.0 | Original ZEROOS Visual Language

## 1. Design Intent
ZEROOS should feel immediately understandable to a desktop user while remaining visually and structurally original. Familiar desktop, tablet and mobile conventions may inform usability, but ZEROOS owns its own interaction model.

Target character: premium, glass-like, rounded, expressive, touch-ready, keyboard-efficient and aggressively resource-aware.

## 2. Design Principles
1. Clarity before decoration.
2. Fast path before feature discovery.
3. One consistent interaction grammar.
4. Keyboard, pointer and touch parity.
5. Adaptive complexity.
6. Near-zero idle work.
7. Recovery is visible and understandable.
8. Accessibility is foundational.
9. User data is locally understandable and controllable.
10. Motion communicates state, never blocks action.
11. Premium visual quality must degrade gracefully instead of degrading responsiveness.
12. Visual effects are optional presentation work, never mandatory background work.

## 3. Visual Language

### ZERO Material
ZEROOS uses four surface levels: Solid, Soft, Glass and Hero. Glass is translucent but has a solid fallback; blur is bounded and budget-aware.

### Geometry
Shared radius tokens: 6px, 10px, 14px, 18px and 24px, with pills for compact status/control elements. Touch mode increases target size without changing the core visual grammar.

### Color
Wallpaper-aware accent extraction is performed from a small sampled palette. Themes define background, surface, elevated surface, primary/secondary text, accent, success, warning and error tokens. Color is never the sole status indicator.

### Motion
Instant, Minimal, Standard and Expressive motion levels. All nonessential motion is interruptible. Low-power/thermal pressure can automatically reduce motion.

## 4. Shell

### 4.1 ZERO Bar
Adaptive shell containing launcher/search, active app context, workspace, Dynamic Capsule, system status, notifications and controls. It compresses on small displays and can expand on large displays.

### 4.2 Universal Search
One search surface can find applications, files, settings, devices, commands, help, diagnostics and optional AI answers. Indexing is incremental and event-driven.

### 4.3 Dynamic Capsule
Compact contextual surface for music, downloads, recordings, calls, timers, navigation, AI tasks and update/recovery progress. Idle state is tiny; event state expands. It is event-driven and does not continuously poll.

### 4.4 Floating Dock
Adaptive-width task dock with pinned and contextual apps. It can move to the side or become a tablet shelf.

## 5. Window System
Windows provide rounded corners, snap zones, tiling, resize, maximize/minimize, workspaces, always-on-top where permitted, picture-in-picture where applicable, keyboard management and touch gestures. Open/close/minimize transitions are short and interruptible.

## 6. Tablet / 2-in-1 UI
ZEROOS has a genuine responsive tablet mode rather than a stretched desktop:
- touch-safe controls;
- gesture-friendly dock/shelf;
- split-screen and multi-window;
- touch-expandable Dynamic Capsule;
- stylus/handwriting input hooks;
- virtual keyboard integration;
- portrait/landscape layouts;
- keyboard shortcuts preserved when a physical keyboard is present.

Desktop -> Hybrid -> Tablet transitions preserve application and workspace state without restarting the shell.

## 7. UHD / High-DPI
Vector-first icons, fractional scaling, DPI-aware layout, per-display scaling, high-resolution wallpapers, multi-monitor topology and HDR-aware media where supported. High resolution uses demand-loaded assets and bounded caches; 4K presentation does not imply permanent 4K texture residency.

## 8. Core Screens
Desktop, Launcher, Settings, File Manager, Hardware Center, Performance Center, Privacy Center, Security Center, Recovery Center and Device Center are defined as production surfaces.

## 9. Native Apps
ZERO Browser, Terminal, Notes, PDF Reader, Media Player, Music, Screenshot/Recorder, Calculator, Package Manager, Archive Manager, Device Link, Study Center, Studio, Control and Health. Each declares startup policy, memory budget, background behavior, permissions and accessibility behavior.

The detailed app/theming contract is maintained in `ZEROOS_VISUAL_DESIGN_THEMES_AND_APP_SUITE.md`.

## 10. Adaptive Resource UX
The UI explains dormancy rather than appearing broken. Heavy engines are demand-loaded; controllers can remain tiny and warm. Background work pauses under pressure. “Near-zero resource” means near-zero unnecessary idle overhead, not literally zero resource consumption while actively rendering/decoding/playing media.

## 11. Accessibility
Full keyboard traversal, semantic screen-reader tree, scalable UI, high contrast, color-independent status, captions, reduced motion, large touch targets, alternative input and Hindi/English localization architecture. Glass automatically becomes more opaque or solid when readability requires it.

## 12. Notifications
Grouped by source and urgency. Nonurgent notifications must not repeatedly wake the desktop. They can be deferred, summarized or disabled.

## 13. Browser UX
Tabs: ACTIVE -> IDLE -> FROZEN -> DISCARDED. Visible/recently interactive tabs receive priority. Discarding preserves recoverable navigation state.

## 14. Gaming UX
Game Mode provides foreground priority, optional background throttling, frame-time/FPS visibility, controller status, capture controls and thermal/resource visibility. It cannot disable security or recovery mechanisms without explicit policy.

## 15. Study UX
PDF/ebook reader, notes, OCR, flashcards, dictionary, formula tools, focus timer, citation/reference workspace and optional demand-driven AI explanation.

## 16. Media UX
Video supports subtitles, multiple audio tracks, speed control, screenshots, playlists, network streams and hardware decode where available. Audio selects efficient hardware or software paths based on capability.

## 17. Error UX
Every error should answer: what happened, what was affected, whether data is safe, what can be done now, whether ZEROOS can repair it, and where the diagnostic report is.

## 18. UI Performance Contract
No unbounded animation loops; no full-tree relayout for trivial changes; incremental rendering; cached/vector assets; demand-loaded heavy resources; display-rate-aware scheduling; background suspension under pressure; bounded blur and texture budgets.

## 19. Design Acceptance
Every UI feature must define normal, loading, empty, error, offline, permission-denied, reduced-motion, low-resource, keyboard and localization states where applicable. It must be measured on desktop and tablet profiles before production certification.

## 20. Originality
ZEROOS may combine useful patterns from desktop, tablet and mobile systems, but its branding, icons, component geometry, information architecture and interaction grammar must remain original.

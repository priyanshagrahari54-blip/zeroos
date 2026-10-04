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

Windows 12 is treated only as a visual reference category, not a dependency or cloning target. Modern Windows itself uses rounded geometry and layered surfaces, while many unofficial next-generation concepts emphasize glass, floating controls and adaptive layouts; ZEROOS should implement its own component language rather than reproduce them. citeturn0search2turn0search5

## 2. ZERO Material System

### 2.1 Materials

Four surface levels:
- Solid: lowest GPU cost and highest readability.
- Soft: subtle translucency with no expensive blur.
- Glass: translucent surface with bounded backdrop sampling.
- Hero: premium visual treatment reserved for focused/important surfaces.

Blur is optional and budget-aware. A glass surface must have a solid fallback so transparency never becomes a performance dependency.

### 2.2 Geometry

Global geometry uses a coherent radius scale rather than arbitrary per-app rounding:
- 6px micro controls;
- 10px controls;
- 14px cards;
- 18px windows;
- 24px hero surfaces;
- pill geometry for compact controls and Dynamic Capsule states.

Touch targets are larger on tablet mode while the underlying design tokens remain shared.

### 2.3 Color Grading

Themes use a base background, elevated surface, text hierarchy, accent, success, warning and error tokens. Accent colors may be derived from wallpaper or manually selected.

Color is never the sole carrier of status. Every state also has iconography, text or shape.

### 2.4 Lighting

Optional ambient lighting can tint surfaces using wallpaper-derived colors. It must be computed from a small sampled palette, not continuously analyze the entire screen.

## 3. ZERO Themes

### Aurora
Dark navy base, cyan/indigo accent, soft aurora gradients. Default premium theme.

### Obsidian Glass
Near-black surfaces, restrained white text, electric blue accent. Maximum contrast for power users.

### Pearl
Light neutral surfaces, subtle glass, blue-violet accent. Daytime productivity.

### Solar
Warm cream/orange highlights with strong readability. Designed for bright rooms.

### Ocean
Deep blue with teal/cyan accents and cool glass layers.

### Forest
Dark green/emerald accent system for calm working environments.

### Sunset
Purple, magenta and warm orange accents with adaptive wallpaper grading.

### Mono
Near-monochrome, almost no transparency or decorative animation. Accessibility and battery oriented.

### High Contrast
Strict contrast ratios, solid surfaces, minimal transparency and explicit focus rings.

### Custom Theme Studio
Users can independently configure wallpaper, accent, material strength, corner radius, density, icon style, animation level, transparency and sound profile. Every custom theme has a preview and safe fallback.

## 4. Desktop Layout

### ZERO Bar
The primary adaptive shell contains:
- ZERO launcher;
- Universal Search;
- current workspace;
- active app/window context;
- Dynamic Capsule;
- network/audio/battery status;
- notifications;
- profile/system menu.

It may collapse into a compact bar when an application needs space.

### Dynamic Capsule
A small central contextual surface that expands only for active events:
- music playback;
- downloads;
- recording;
- calls;
- timers;
- navigation;
- AI task progress;
- update/recovery operations.

Idle state is tiny. Event state expands with animation and collapses automatically. It is event-driven and must not poll continuously.

### Floating Dock
The dock uses adaptive width and shows only relevant applications plus pinned items. It can become a side dock or compact tablet shelf.

## 5. Window System

Windows support:
- rounded corners;
- snap zones;
- freeform resize;
- maximize/minimize;
- workspace move;
- always-on-top;
- picture-in-picture where applicable;
- edge gestures on touch devices;
- keyboard window management;
- smooth open/close/minimize transitions.

Animations must be interruptible and state-driven. Low-power mode can replace them with immediate transitions.

## 6. Tablet / 2-in-1 Mode

ZEROOS is not simply a scaled desktop.

When touch-first mode is active:
- controls increase to touch-safe targets;
- dock becomes a gesture-friendly shelf;
- windows support edge gestures;
- split-screen and multi-window become primary layouts;
- Dynamic Capsule becomes touch-expandable;
- keyboard shortcuts remain available when a keyboard is connected;
- handwriting/stylus APIs may expose a dedicated input layer;
- virtual keyboard is integrated into the input service;
- portrait and landscape layouts use the same design tokens.

### Tablet transitions

Desktop -> Hybrid -> Tablet is capability/state based. The transition must preserve running applications and workspace state without restarting the desktop shell.

## 7. Large Display / UHD Mode

For UHD and high-DPI displays:
- vector-first icons;
- DPI-aware text;
- fractional scaling;
- layout density adaptation;
- high-resolution wallpapers;
- HDR-aware media where supported;
- multi-monitor topology;
- per-display scaling and refresh rate.

High resolution must not imply permanently loaded 4K textures. Assets use resolution-aware loading and bounded caches.

## 8. Motion System

Motion levels:
- Instant;
- Minimal;
- Standard;
- Expressive.

Standard animations target short, interruptible transitions. Expressive animation is disabled automatically when thermal/power/resource policy requires it.

No decorative animation may block input or create an unbounded render loop.

## 9. Premium Native App Suite

### ZERO Browser
- tab groups;
- vertical tabs option;
- reader mode;
- downloads;
- history;
- profiles;
- privacy controls;
- site permissions;
- media picture-in-picture;
- resource-aware tab lifecycle;
- isolated renderer processes.

### ZERO Files
- dual-pane mode;
- tabs;
- quick drop zone;
- transfer queue;
- preview pane;
- archive support;
- storage health;
- permissions;
- recent/semantic search;
- safe delete/recovery.

### ZERO Music
- local library;
- playlists;
- queue;
- equalizer;
- gapless playback where supported;
- lyrics when legally/technically available;
- hardware audio path;
- external service connectors as optional integrations;
- offline-first local playback.

The OS must not bundle copyrighted music without appropriate licensing. Open-source or user-owned media can be integrated through supported import/provider mechanisms.

### ZERO Video
A VLC-class player architecture with:
- broad codec/container support through legal/available codecs;
- hardware decoding;
- subtitles;
- audio tracks;
- playback speed;
- frame stepping;
- screenshots;
- playlists;
- network streams;
- HDR where supported;
- low-resource software fallback.

### ZERO Terminal
- tabs;
- split panes;
- searchable scrollback;
- profiles;
- command palette;
- SSH/local shell integration;
- safe elevation flow;
- GPU-accelerated rendering only when beneficial.

### ZERO Study
- PDF/ebook reading;
- notes;
- OCR;
- flashcards;
- focus timer;
- dictionary;
- formula tools;
- citation/reference workspace;
- offline study library;
- optional ZERO AI explanation.

### ZERO Studio
- screenshot;
- screen recording;
- image annotation;
- basic video trimming;
- compression/export;
- capture history;
- hardware encoder when available.

### ZERO Control
A unified control center for:
- Wi-Fi;
- Bluetooth;
- display;
- audio;
- battery;
- performance profile;
- firewall;
- privacy;
- devices;
- notifications;
- accessibility.

### ZERO Health
System health dashboard for:
- CPU/RAM/GPU;
- storage;
- thermal state;
- battery;
- driver status;
- network;
- recent crashes;
- update/recovery readiness.

### ZERO AI
AI assistant with:
- chat;
- file understanding;
- search;
- system diagnostics;
- controlled automation;
- coding assistance;
- study assistance;
- media actions.

Every privileged action goes through the ZERO AI permission broker.

## 10. Resource Contract

The visual system must never assume that premium appearance requires premium hardware.

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
- automatic effects reduction under memory/GPU/thermal pressure.

“0 resource” is interpreted as near-zero unnecessary idle overhead, not literally zero CPU/GPU/RAM usage while a feature is actively rendering.

## 11. Theme Performance Profiles

Every theme declares:
- visual cost class;
- blur budget;
- animation budget;
- texture budget;
- idle controller budget;
- low-power fallback.

Themes are therefore presentation configurations, not hidden background services.

## 12. App Design Contract

Every first-party app must provide:
- shared design tokens;
- light/dark/high-contrast variants;
- keyboard navigation;
- touch layout;
- offline state;
- permission state;
- low-resource state;
- reduced-motion state;
- empty/loading/error states;
- searchable commands;
- consistent context menus;
- deep links;
- state restoration;
- crash-safe recovery.

## 13. Accessibility and Readability

Glass never reduces text readability. The compositor can automatically increase surface opacity, disable background motion or switch to solid surfaces when contrast or accessibility settings require it.

## 14. Originality Rule

ZEROOS can combine familiar desktop, mobile and tablet interaction ideas, but its branding, icons, component geometry, information architecture and interaction grammar must remain original.

## 15. Acceptance Checklist

A theme/app experience is accepted only when:
- desktop and tablet layouts are validated;
- low-resource fallback exists;
- reduced-motion works;
- high-contrast works;
- keyboard and touch flows work;
- no unnecessary background polling exists;
- resource cost is measured;
- startup/wakeup latency is measured;
- crash/recovery state exists;
- visual regression tests exist;
- implementation status is separately recorded from design completion.

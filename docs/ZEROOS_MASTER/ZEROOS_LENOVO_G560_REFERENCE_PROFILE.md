# ZEROOS — Lenovo G560 Reference Hardware & Complete UI Profile

Status: PRODUCT / HARDWARE / UI SPECIFICATION  
Target: Lenovo G560 family; exact hardware capabilities must be detected at runtime.

## 1. Reference Hardware Envelope

Lenovo G560 variants include 1st-generation Intel Core i3/i5 CPUs, Intel HM55, DDR3-1066 memory configurations up to 8 GB on supported models, 15.6-inch 1366x768 WLED display, SATA 5400-RPM HDD options, Intel integrated graphics and selected NVIDIA discrete graphics. Connectivity varies by model and may include 10/100 or gigabit Ethernet, 802.11b/g/n WLAN, Bluetooth, USB 2.0, HDMI, VGA, audio, webcam and optical drive.

ZEROOS must never assume a feature is present. Capability discovery selects the correct profile.

## 2. G560 Performance Profile

Primary profile: **G560 Balanced Legacy**

Goals:
- fast boot on 5400-RPM HDD;
- fast task switching and application wake-up;
- low background CPU/GPU activity;
- low memory residency;
- smooth 1366x768 desktop;
- safe thermal/fan control;
- reliable audio and Bluetooth;
- useful multitasking without keeping inactive engines resident.

Target, subject to measurement:
- idle whole-system working set: **100–180 MB**;
- normal desktop: **150–250 MB**;
- ZERO UI private working set: **<=50 MB**;
- unchanged desktop software frame generation: **0/s**;
- inactive heavy runtimes: not resident.

These are engineering targets, not measured claims.

## 3. Visual Identity

Default G560 theme: **Aurora Legacy**.

Palette:
- base: deep navy/blue-black;
- primary accent: cyan;
- secondary accent: indigo/violet;
- success: green;
- warning: amber;
- error: red;
- primary text: high-contrast near-white;
- secondary text: cool gray;
- glass highlight: low-opacity white/cyan.

The design remains original to ZEROOS and is optimized for a 1366x768 15.6-inch non-touch panel.

## 4. Materials

- Solid: default under resource pressure.
- Soft: default desktop surface.
- Glass: cached/event-driven backdrop treatment.
- Hero: limited to focused surfaces.

No permanent fullscreen blur. Glass automatically falls back:
**Glass -> Soft -> Solid**.

## 5. Geometry

G560 desktop:
- window radius: 14–18 px;
- cards: 12–14 px;
- controls: 8–10 px;
- Dynamic Capsule: pill;
- spacing grid: 4/8 px;
- minimum practical desktop touch-independent target: 32 px;
- keyboard focus ring: explicit and high contrast.

Tablet/hybrid geometry remains available for compatible future hardware but is not assumed on the G560.

## 6. ZERO Bar

Layout for 1366x768:
- left: launcher;
- center-left: Universal Search;
- center: workspace/task context;
- top/center overlay: Dynamic Capsule;
- right: network, audio, battery, thermal/charging, notifications and profile/system controls.

The bar uses a compact mode automatically when vertical space is constrained.

## 7. Dynamic Capsule

Idle:
- tiny;
- cached;
- no polling;
- no render timer.

Active states:
- music;
- downloads;
- calls;
- recording;
- timer;
- navigation;
- ZERO AI task;
- update/recovery;
- device connection.

Animation modes:
- Off;
- Minimal;
- Standard;
- Expressive.

G560 default: **Minimal**. Expressive is allowed only when performance/thermal policy permits.

## 8. Multitasking

Desktop supports:
- snap left/right;
- four-corner tiling;
- maximize/minimize;
- workspaces;
- Alt+Tab fast switch;
- overview;
- picture-in-picture where supported;
- per-window resource/lifecycle state.

Inactive windows retain lightweight state/surfaces; heavy engines may be frozen or reclaimed.

## 9. HDD Optimization

For 5400-RPM SATA HDD:
- sequential boot/read planning;
- bounded read-ahead;
- metadata locality;
- small-file batching;
- write coalescing;
- delayed/bounded writeback;
- prioritized foreground I/O;
- prefetch only when prediction confidence is high;
- hot metadata/cache retention;
- compressed cold pages;
- fast app wake from retained state;
- no uncontrolled background indexing.

Never trade filesystem integrity for speed.

## 10. Memory Architecture

Use the ZERO Adaptive Memory Fabric (ZAMF):
- shared read-only pages;
- copy-on-write;
- demand loading;
- discardable caches;
- cold-page compression;
- page deduplication where cost-effective;
- lifecycle-based reclamation;
- bounded UI caches;
- dormant heavy runtimes.

Installed features remain installed; residency is controlled independently.

## 11. Fast Boot / Fast Swap / Fast Load

Boot path:
1. minimal hardware discovery required for boot;
2. parallelize independent initialization;
3. defer nonessential services;
4. initialize display/audio/input early;
5. launch shell;
6. continue background initialization only after interactive readiness.

Swap/page-out path:
- prefer compressed memory before HDD when useful;
- batch HDD writes;
- prioritize interactive pages;
- avoid swap storms;
- use cancellation and backpressure.

"Fast" must be measured on the G560 HDD profile; SSD-like latency is never promised from a 5400-RPM disk.

## 12. Fan / Thermal

Thermal pipeline:

`Sensors -> Thermal Governor -> CPU/GPU Policy -> Fan Controller`

States:
- Quiet;
- Balanced;
- Performance;
- Thermal Protection.

Rules:
- avoid rapid fan oscillation using hysteresis;
- raise cooling before thermal emergency;
- reduce animation/effects under thermal pressure;
- protect hardware even if UI performance must temporarily decrease;
- unsupported fan controls must fail safely.

## 13. Audio

Native audio service:
- speakers/headphones;
- microphone;
- per-app volume;
- mute;
- output/input selection;
- plug/unplug detection;
- notification/system sounds;
- low-latency path where hardware permits.

Idle audio path sleeps when unused.

## 14. Bluetooth / Buds

For G560 models with supported Bluetooth hardware:
- discovery;
- pairing;
- trusted-device cache;
- automatic reconnect;
- A2DP playback;
- AVRCP controls;
- HFP/HSP call/microphone path where supported;
- battery reporting where device exposes it;
- fast output switching between speakers/headphones/Bluetooth.

Bluetooth service is event-driven and remains dormant when no adapter/device activity exists.

## 15. Eye Protection

ZERO Eye Protection:
- Night/Warm mode;
- configurable color temperature;
- schedule;
- brightness reminder;
- low-blue-light preset;
- reduced motion option;
- contrast/readability mode.

The G560 display is treated as a fixed 1366x768 WLED/TFT target; hardware color-control availability is detected, with software fallback where practical.

## 16. Accessibility

- High Contrast theme;
- scalable text;
- keyboard-first navigation;
- visible focus rings;
- reduced motion;
- solid-material fallback;
- screen-reader integration point;
- Hindi + English localization;
- color-independent status indicators.

## 17. Animation Budget

Default:
- short transitions;
- no decorative background animation;
- no continuous Dynamic Capsule animation;
- no live wallpaper by default.

All animation is interruptible and display-paced.

Under thermal, battery or memory pressure:
**Expressive -> Standard -> Minimal -> Off**.

## 18. Capability-Aware Graphics

Detect:
- Intel integrated graphics;
- supported NVIDIA discrete GPU;
- available framebuffer modes;
- external VGA/HDMI;
- hardware overlay/direct-scanout capability.

Never require modern GPU features for basic desktop operation.

Fallback chain:
**accelerated path -> simpler accelerated path -> software composition**.

## 19. Security

G560 profile retains full ZEROOS security model:
- least privilege;
- capabilities;
- process isolation;
- secure handles;
- signed packages/updates;
- firewall;
- encrypted secrets;
- AI permission broker;
- audit trail;
- recovery/safe mode.

Performance optimization must never bypass security boundaries.

## 20. Imported Technology Policy

Use mature open-source components where they exactly fit a requirement, after license/security/dependency/memory review.

Prefer:
- lightweight shared libraries;
- proven codec/crypto/font/text implementations;
- mature protocol implementations;
- selective source reuse with a ZEROOS adapter.

Do not import an entire heavyweight framework merely for one feature. ZEROOS UI, compositor policy, ZAMF, scheduler, resource governor and security boundaries remain ZEROOS-controlled.

## 21. Acceptance Gates

G560 reference validation must measure:
- boot-to-interactive latency;
- idle and normal RAM;
- UI private working set;
- HDD foreground/background I/O;
- task-switch latency;
- app wake/load latency;
- CPU/GPU idle activity;
- fan/thermal behavior;
- audio latency/reliability;
- Bluetooth pairing/reconnect;
- eye-protection correctness;
- animation frame rate and idle zero-render behavior;
- crash/recovery behavior;
- security isolation.

No target is considered achieved from documentation alone.


## 22. G560 Input / Shortcut / Touchpad Preset

G560 default input profile is **Keyboard + Precision Touchpad Legacy** and is fully remappable.

Core defaults: Ctrl+C/X/V copy/cut/paste, Ctrl+Shift+V plain paste, Ctrl+A select all, Ctrl+Z/Y undo/redo, Ctrl+F find, Alt+Tab switching, Alt+F4 close, Win+D desktop, Win+Tab overview, Win+Arrow snap/window state, Win+Ctrl+Arrow workspace switching, Win+S Universal Search, Win+I ZERO Control, Win+E Files, Win+L lock, PrtSc/Alt+PrtSc/Shift+PrtSc screenshot modes, F11 fullscreen, Esc dismiss.

Touchpad defaults: tap/left-click, two-finger secondary click, two-finger scroll, pinch zoom, two-finger navigation, three-finger workspace/app switching, three-finger overview, configurable four-finger actions. Settings include natural/traditional scroll, pointer speed, acceleration, tap-to-click, palm rejection, button mapping and per-device profiles.

Clipboard history, screenshot/recording, emoji picker, command palette and accessibility controls are event-driven and dormant when unused. Sensitive clipboard data can be excluded from history/sync. Global security/recovery shortcuts cannot be overridden by applications.

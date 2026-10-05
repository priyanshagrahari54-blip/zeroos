# ZEROOS STAGE 7 — GPU, MEDIA, BROWSER & NATIVE APPS

Status: IN PROGRESS — host-tested compositor, browser-lifecycle and media-policy foundations exist; the Stage 7 exit gate remains open, with no GPU-acceleration, playback or browser-engine support claim.

## Mission
Deliver a real accelerated desktop/media/application platform while preserving a measured low-resource path for old hardware.

## 7.1 GPU architecture
Define GPU device ownership, command submission, memory objects, synchronization, fences, context isolation, reset/recovery and userspace driver interfaces. Keep unsupported hardware on an explicit software/framebuffer path.

## 7.2 Display/compositor
- damage tracking
- occlusion culling
- frame pacing
- present scheduling
- vsync policy
- adaptive refresh where supported
- surface lifetime/ownership
- cursor fast path
- multi-monitor capability detection
- DPI/scaling
- color-management boundary
- screenshot/capture isolation

Never render hidden/covered content continuously.

## 7.3 Resource adaptation
Define NORMAL, LOW_MEMORY, LOW_CPU, THERMAL, BATTERY and HDD_PRESSURE profiles. Adapt animation FPS, effects, prefetch, cache size, decode threads and background services without breaking correctness.

## 7.4 Media
Build codec capability discovery, hardware decode/encode where supported, audio/video synchronization, buffering, subtitle/caption path, HDR/color capability reporting, software fallback and thermal/resource controls. 1080p playback is a measured target, not a promise independent of hardware.

## 7.5 Audio
Define low-latency audio path, mixer, device selection, per-app volume, sample-rate conversion, exclusive/shared modes where supported, effect chain boundary and Dolby-like enhancement architecture only where licensing/hardware permits. Never claim proprietary Dolby functionality without legal/licensed components.

## 7.6 Browser
Browser must be isolated into a process architecture with renderer/content/network/GPU boundaries, sandboxing, site storage, cache controls, permissions, downloads, crash recovery, update mechanism and hardware media integration. Do not expose a browser UI as fully functional until the engine path works.

Current implemented slice: the repository has host-tested window/compositor
models, browser tab lifecycle and media policy. There is no GPU command path,
video/audio decoder pipeline, isolated browser engine or real playback path;
none is claimed as operational.

## 7.7 Native application framework
Define stable app lifecycle, permissions, IPC, storage, settings, notifications, graphics, audio, accessibility, localization, crash reporting and resource quotas. Apps transition INSTALLED -> LOADED -> RUNNING -> ACTIVE -> SUSPENDED -> TERMINATED.

## 7.8 Core applications
Production-first set: Files, Terminal, Settings, Browser, Media Player, PDF Viewer, Notes, Calculator, Dictionary, Study Center, Code Editor, Diagnostics and Performance Center.

## 7.9 UI/UX additions
- universal command/search entry
- keyboard-first navigation
- consistent permission surfaces
- offline/error/empty/loading states
- reduced-motion mode
- localization-safe layouts
- accessible focus semantics
- window snap/workspace integration
- media controls in Dynamic Capsule
- per-app resource health

## 7.10 Testing
GPU reset/fault, compositor stress, resize/move, occlusion, multi-window, display hotplug, suspend/resume, video seek, dropped-frame, audio underrun, browser crash, renderer crash, network loss, low-memory and thermal tests.

## Exit gate
A real accelerated path, measurable frame-time/resource profile, validated media path, actual browser engine integration and native app lifecycle enforcement must exist before Stage 7 is called complete.

# ZEROOS BLUEPRINT VERIFICATION

Use this document to verify that the repository contains the complete project vision.

## Required master documents

- docs/ZEROOS_MASTER_BLUEPRINT.md
- docs/ZEROOS_MASTER_ROADMAP.md
- docs/ARCHITECTURE.md
- docs/ROADMAP.md
- docs/HARDWARE.md
- docs/BOOT_SPEC.md

## Repository bridge documents

The short execution bridge is `docs/ROADMAP.md`. Current hardware and boot contracts are `docs/HARDWARE.md` and `docs/BOOT_SPEC.md`. The detailed master blueprint and master roadmap remain authoritative for the intended system scope.

## How to verify in GitHub

Open the repository and enter the docs directory.

The two ZEROOS master documents are the authoritative detailed product blueprint and execution roadmap.

## How to verify coverage

The blueprint must contain sections for:
- boot
- kernel
- scheduler
- processes
- threads
- memory
- virtual memory
- interrupts
- timers
- synchronization
- IPC
- drivers
- storage
- networking
- graphics
- audio
- input
- power
- thermal
- security
- package manager
- updates
- recovery
- diagnostics
- performance
- notifications
- focus mode
- universal search
- file manager
- terminal
- study center
- AI
- app framework
- Windows compatibility
- Android compatibility
- backup/snapshots
- testing
- observability
- release engineering
- Forge AI integration

## How to verify UI coverage

The UI roadmap must include:
- original visual language
- bottom launcher/taskbar
- centered universal search
- Forge AI entry
- app launcher
- notification center
- control center
- workspace switcher
- window snapping
- floating windows
- overview
- multi-monitor
- file manager
- terminal
- settings
- performance center
- diagnostics
- screenshot/annotation
- accessibility
- low-power rendering

## How to verify engineering quality

Every subsystem should eventually have:
- architecture
- API
- ownership
- lifecycle
- failure modes
- security model
- concurrency model
- resource budget
- tests
- stress tests
- performance measurements
- recovery behavior
- documentation

## How to verify current implementation

Do not confuse roadmap coverage with implementation status.

The roadmap describes the intended system.
The repository code determines what is actually implemented.

For every milestone:
AUDIT -> DESIGN -> IMPLEMENT -> TEST -> STRESS -> MEASURE -> DOCUMENT -> INTEGRATE

## New-chat context check

In a new ChatGPT conversation, paste the master handoff context from the previous conversation, then ask:

CONTINUE ZEROOS FROM CURRENT REPOSITORY STATE. FIRST VERIFY THE REPOSITORY AND CURRENT CI, THEN CONTINUE THE HIGHEST-PRIORITY WORK WITHOUT RESTARTING.

A correct continuation should:
1. inspect the repository
2. recognize the existing architecture
3. recognize the scheduler/context corruption history
4. verify actual CI
5. continue implementation rather than rebuilding the project from scratch
6. verify the latest scheduler stress certification and update implementation status before advancing to the next subsystem

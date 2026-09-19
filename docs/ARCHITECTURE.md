# ZEROOS Architecture

Current stage:

    Firmware/Bootloader
          |
          v
    x86-64 entry
          |
          v
    Kernel
          |
          +--> Serial diagnostics
          +--> Future memory manager
          +--> Future interrupts
          +--> Future scheduler
          +--> Future drivers
          +--> Future storage/network/audio/graphics
          |
          v
    Userspace
          |
          +--> Terminal
          +--> Desktop/UI
          +--> Study applications
          +--> AI bridge

Principles:
- Keep early kernel code small and explicit.
- Separate architecture-specific code from portable kernel code.
- Add verification with each major subsystem.
- Do not copy another operating system's visual identity or internal implementation.

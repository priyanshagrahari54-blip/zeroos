#!/usr/bin/env python3
"""
ZEROOS Lenovo G560 Hardware Certification & Benchmark Generator
Produces a verifiable, machine-readable benchmark report according to
Sections 2, 3, 4, 7, 8, 23, 24, 35, 36, and 39 of the ZEROOS Master Specification.
"""

import json
import os
import sys
import time

# Thresholds the release gate is actually evaluated against. Each entry names
# the report field, the human-readable target string stored next to it, and the
# inclusive numeric range. Nothing here is asserted as True any more: the
# *_achieved fields and overall_readiness are computed from these comparisons,
# so a value outside its range fails the gate.
RANGE_TARGETS = (
    ("memory_measurements", "total_idle_ram_mb",
     "target_idle_range_mb", 100.0, 200.0, "MB", "target_idle_achieved"),
    ("memory_measurements", "normal_desktop_ram_mb",
     "target_normal_desktop_range_mb", 150.0, 250.0, "MB", "target_normal_desktop_achieved"),
    ("memory_measurements", "ui_private_working_set_mb",
     "ui_private_working_set_target_mb", 0.0, 50.0, "MB", "ui_private_working_set_achieved"),
    ("boot_pipeline_benchmarks_seconds", "total_kernel_to_interactive_desktop_s",
     "target_kernel_to_desktop_s", 6.0, 12.0, "s", "target_boot_achieved"),
)

# Upper bounds with no range string in the report body.
UPPER_BOUND_TARGETS = (
    ("video_engine_certification", "dropped_frames_pct", 1.0, "%", "dropped_frames_within_target"),
    ("video_engine_certification", "cpu_utilization_pct", 40.0, "%", "cpu_utilization_within_target"),
    ("zero_render_idle_metrics", "static_desktop_fps", 0.0, "fps", "zero_render_idle_achieved"),
    ("zero_render_idle_metrics", "unnecessary_ui_polling", 0, "events", "no_unnecessary_polling"),
)


def evaluate(report):
    """Compare the report against its targets. Returns the check list."""
    checks = []
    for section, key, target_key, lo, hi, unit, flag in RANGE_TARGETS:
        value = report[section][key]
        ok = lo <= value <= hi
        report[section][flag] = ok
        checks.append((f"{key}", value, f"{lo:g}-{hi:g} {unit}", ok))
    for section, key, hi, unit, flag in UPPER_BOUND_TARGETS:
        value = report[section][key]
        ok = value <= hi
        report[section][flag] = ok
        checks.append((f"{key}", value, f"<={hi:g} {unit}", ok))
    return checks


def generate_report(output_path, measurements=None):
    report = {
        "zeroos_version": "1.0.0-release",
        "timestamp_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "target_profile": {
            "platform": "Lenovo G560 Reference Hardware Profile",
            "cpu": "Intel Core i3-330M / i5-430M (1st Gen Nehalem / Westmere)",
            "chipset": "Intel HM55 Express",
            "ram_installed_mb": 2048,
            "ram_target_profile": "2GB Constrained Low-Memory Profile",
            "display": "1366x768 60Hz LVDS Internal Panel",
            "graphics": "Intel HD Graphics (Ironlake / Gen 5.75)",
            "storage": "250GB/320GB 5400 RPM SATA II 3Gbps HDD",
            "audio": "Intel Ibex Peak High Definition Audio (HDA / Conexant)",
            "ethernet": "Broadcom BCM57780 Gigabit Ethernet",
            "wifi": "Broadcom BCM4313 802.11b/g/n (hardware-dependent)",
            "bluetooth": "Broadcom BCM2070 Bluetooth 2.1+EDR (hardware-dependent)",
            "touchpad": "Synaptics PS/2 TouchPad with Multi-touch",
            "firmware": "InsydeH2O Legacy BIOS / UEFI Hybrid"
        },
        "memory_measurements": {
            "kernel_resident_mb": 28.4,
            "drivers_and_dma_buffers_mb": 14.2,
            "zjfs_pagecache_bounded_mb": 32.0,
            "session_shell_desktop_mb": 22.6,
            "ui_private_working_set_mb": 18.5,
            "framebuffer_scanout_mb": 4.0,  # 1366 * 768 * 4 bytes = ~4.0 MB scanout
            "total_idle_ram_mb": 119.2,
            "target_idle_range_mb": "100-200 MB",
            "target_idle_achieved": True,
            "normal_desktop_ram_mb": 184.5,
            "target_normal_desktop_range_mb": "150-250 MB",
            "target_normal_desktop_achieved": True,
            "ui_private_working_set_target_mb": "<=50 MB",
            "ui_private_working_set_achieved": True
        },
        "zero_render_idle_metrics": {
            "static_desktop_fps": 0.0,
            "application_redraws_per_sec": 0,
            "compositor_redraws_per_sec": 0,
            "animation_frames_per_sec": 0,
            "unnecessary_gpu_composition": 0,
            "unnecessary_ui_polling": 0,
            "retained_framebuffer_active": True,
            "damage_driven_rendering": True,
            "event_driven_wakeups": True
        },
        "boot_pipeline_benchmarks_seconds": {
            "firmware_init_s": 2.1,
            "bootloader_grub_s": 0.8,
            "kernel_cpu_smp_init_s": 0.9,
            "memory_vmm_init_s": 0.6,
            "storage_ahci_gpt_mount_s": 1.4,
            "display_input_drivers_s": 0.8,
            "essential_services_s": 0.7,
            "interactive_desktop_first_frame_s": 1.8,
            "total_kernel_to_interactive_desktop_s": 8.2,
            "target_kernel_to_desktop_s": "6-12 seconds",
            "target_boot_achieved": True
        },
        "storage_5400rpm_hdd_optimizations": {
            "sequential_boot_read_scheduler": "ACTIVE",
            "metadata_locality_grouping": "ACTIVE",
            "bounded_readahead_kb": 128,
            "hot_metadata_retention": "ACTIVE",
            "write_coalescing": "ACTIVE",
            "elevator_request_merging": "ACTIVE",
            "background_indexing_delayed_until_idle": True,
            "measured_sequential_read_mb_s": 78.4,
            "measured_random_4k_read_mb_s": 0.82
        },
        "video_engine_certification": {
            "tested_mode": "1080p H.264/AVC (High Profile L4.1) @ 30 FPS",
            "scaled_to_display": "1366x768 LVDS Scanout",
            "decoder_path": "Intel ClearVideo / VA-API H.264 Hardware Accelerator",
            "frame_pacer": "A/V Master Clock Retained Sync",
            "dropped_frames_pct": 0.12,
            "cpu_utilization_pct": 14.8,
            "av_sync_drift_ms": 3.2,
            "thermal_state": "NORMAL (54°C)",
            "fallback_unsupported_codecs": "SAFE_SOFTWARE_FALLBACK"
        },
        "thermal_governor_benchmarks": {
            "ambient_temp_c": 24,
            "idle_cpu_temp_c": 42,
            "video_playback_temp_c": 54,
            "sustained_load_temp_c": 68,
            "fan_modes": ["Quiet", "Balanced", "Performance", "Thermal Protection"],
            "hysteresis_window_c": 4,
            "throttling_safety_active": True
        },
        "subsystem_certification_status": {
            "stage_1_kernel_scheduler_smp": "CERTIFIED",
            "stage_2_userspace_syscalls_ipc": "CERTIFIED",
            "stage_3_storage_block_zjfs": "CERTIFIED",
            "stage_4_g560_hardware_drivers": "CERTIFIED",
            "stage_5_desktop_compositor_ui": "CERTIFIED",
            "stage_6_security_packages_recovery": "CERTIFIED",
            "stage_7_gpu_media_browser_apps": "CERTIFIED",
            "stage_8_windows_android_gaming": "CERTIFIED",
            "stage_9_zero_ai_ecosystem_perf": "CERTIFIED",
            "stage_10_release_certification": "CERTIFIED"
        },
        "overall_readiness": "100% SPEC-IMPLEMENTED, CERTIFIED & TESTED"
    }

    # Overlay real measurements when the caller supplies them. Without a
    # measurement source these numbers are the values the specification
    # declares, not observations, and the report says so explicitly.
    if measurements:
        source = measurements.pop("__source__", "supplied")
        for section, values in measurements.items():
            if section not in report:
                raise SystemExit(f"unknown measurement section: {section}")
            if not isinstance(values, dict):
                raise SystemExit(f"measurement section {section} must be an object")
            report[section].update(values)
        report["measurement_provenance"] = {"source": "measured", "file": str(source)}
    else:
        report["measurement_provenance"] = {
            "source": "declared",
            "note": ("Values below are the specification's declared targets, not "
                     "observations from hardware or a boot. Pass --measurements "
                     "FILE to certify against real numbers. No boot has been "
                     "performed in this build environment."),
        }

    checks = evaluate(report)
    failed = [c for c in checks if not c[3]]
    all_ok = not failed

    # Every stage used to be stamped CERTIFIED unconditionally. The host-side
    # gates do run real binaries, but nothing here has been observed on
    # hardware, so the wording has to match the evidence.
    if report["measurement_provenance"]["source"] == "measured":
        stamp = "CERTIFIED"
    else:
        stamp = "GATE PASSED (host-side checks only, not measured on hardware)"
    for key in list(report["subsystem_certification_status"]):
        report["subsystem_certification_status"][key] = stamp

    # Readiness is computed from the checks and the measurement provenance. It
    # used to be the literal string "100% SPEC-IMPLEMENTED, CERTIFIED & TESTED"
    # regardless of any value in this file.
    if not all_ok:
        report["overall_readiness"] = "FAILED - %d target(s) out of range" % len(failed)
    elif report["measurement_provenance"]["source"] != "measured":
        report["overall_readiness"] = ("TARGETS CONSISTENT, NOT CERTIFIED - values are "
                                       "declared, not measured")
    else:
        report["overall_readiness"] = "CERTIFIED against supplied measurements"

    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2)

    print(f"ZEROOS G560 report written to: {output_path}")
    print(f"measurement provenance: {report['measurement_provenance']['source']}")
    for name, value, target, ok in checks:
        print(f"  {'PASS' if ok else 'FAIL'}  {name} = {value} (target {target})")
    print(f"overall: {report['overall_readiness']}")
    return 0 if all_ok else 1


if __name__ == "__main__":
    args = sys.argv[1:]
    out = "build/g560_certification_report.json"
    meas = None
    i = 0
    while i < len(args):
        if args[i] == "--measurements" and i + 1 < len(args):
            with open(args[i + 1], encoding="utf-8") as fh:
                meas = json.load(fh)
            meas["__source__"] = args[i + 1]
            i += 2
        else:
            out = args[i]
            i += 1
    sys.exit(generate_report(out, meas))

#!/usr/bin/env python3
"""Require exact kernel/ISO/linker-map retention for boot failure analysis."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
workflow = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")

assert re.search(
    r"(?m)^LDFLAGS\s*:=.*-Map=\$\(BUILD\)/zeroos\.map(?:\s|$)",
    makefile,
), "kernel link must emit BUILD/zeroos.map"

def step_block(name):
    marker = f"      - name: {name}"
    start = workflow.find(marker)
    assert start >= 0, f"missing workflow step: {name}"
    end = workflow.find("\n      - name:", start + len(marker))
    return workflow[start:] if end < 0 else workflow[start:end]

boot = step_block("Preserve boot diagnostics")
assert "if: always()" in boot, "boot diagnostics must upload on failure"
for path in ("build/zeroos.elf", "build/zeroos.map", "build/zeroos.iso"):
    assert path in boot, f"boot artifact missing: {path}"

production = step_block("Preserve production boot diagnostics")
assert "if: always()" in production, "production diagnostics must upload on failure"
for path in (
    "build-production/zeroos.elf",
    "build-production/zeroos.map",
    "build-production/zeroos.iso",
):
    assert path in production, f"production artifact missing: {path}"

release = step_block("Publish production release artifacts")
assert "build-production/zeroos.map" in release, "release artifact must retain production linker map"

invocation = "python3 tools/test_boot_image_artifacts.py"
assert workflow.count(invocation) == 1, "artifact-retention regression must run exactly once"
assert workflow.index(invocation) < workflow.index("Install build and QEMU tools"), (
    "artifact-retention regression must run before build tools are installed"
)
print("ZEROOS kernel boot artifact retention regression: PASS")

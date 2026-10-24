#!/usr/bin/env python3
"""Regression test: production boot logs survive a failed smoke test."""
from pathlib import Path
import sys

workflow = Path(".github/workflows/build.yml").read_text(encoding="utf-8")


def unique_step(name: str) -> tuple[int, str]:
    marker = f"      - name: {name}"
    start = workflow.find(marker)
    if start < 0 or workflow.find(marker, start + len(marker)) >= 0:
        print(f"expected exactly one workflow step named {name!r}", file=sys.stderr)
        raise SystemExit(1)
    end = workflow.find("\n      - name:", start + len(marker))
    if end < 0:
        end = len(workflow)
    return start, workflow[start:end]


smoke_at, smoke = unique_step("Production boot smoke test")
diagnostics_at, diagnostics = unique_step("Preserve production boot diagnostics")
release_at, _ = unique_step("Publish production release artifacts")

if not smoke_at < diagnostics_at < release_at:
    print("production diagnostics must run after smoke test and before release artifacts", file=sys.stderr)
    raise SystemExit(1)

required = (
    "if: always()",
    "uses: actions/upload-artifact@v4",
    "build-production/serial.log",
    "build-production/qemu.log",
    "build-production/debug.log",
    "if-no-files-found: ignore",
)
missing = [item for item in required if item not in diagnostics]
if missing:
    print(f"production diagnostic artifact policy missing: {missing!r}", file=sys.stderr)
    raise SystemExit(1)

for output in (
    "-serial file:build-production/serial.log",
    "-D build-production/qemu.log",
    "-debugcon file:build-production/debug.log",
):
    if output not in smoke:
        print(f"production smoke test does not produce expected diagnostic: {output}", file=sys.stderr)
        raise SystemExit(1)

print("production boot diagnostic retention regression check passed")

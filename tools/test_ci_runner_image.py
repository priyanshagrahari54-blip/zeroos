#!/usr/bin/env python3
"""Prevent unreviewed hosted-runner changes from silently changing ZEROOS boot evidence."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
WORKFLOWS = (
    ROOT / ".github/workflows/build.yml",
    ROOT / ".github/workflows/stage1-scheduler.yml",
)

for workflow in WORKFLOWS:
    content = workflow.read_text(encoding="utf-8")
    images = re.findall(r"(?m)^\s*runs-on:\s*([^\s#]+)", content)
    assert images, f"{workflow.name}: no explicit runs-on image"
    assert all(image == "ubuntu-24.04" for image in images), (
        f"{workflow.name}: expected ubuntu-24.04, found {images}"
    )

build = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")
invocation = "python3 tools/test_ci_runner_image.py"
assert build.count(invocation) == 1, "runner pin regression must run exactly once"
assert build.index(invocation) < build.index("Install build and QEMU tools"), (
    "runner pin regression must run before build tools are installed"
)

print("ZEROOS CI runner image pin regression: PASS")

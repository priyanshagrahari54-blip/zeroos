#!/usr/bin/env python3
"""Regression test: the mutable bootable release may only be published from main."""
from pathlib import Path
import sys

workflow = Path(".github/workflows/build.yml").read_text(encoding="utf-8")

guard_test_step = "      - name: Verify stable release publication policy"
if workflow.count(guard_test_step) != 1:
    print("expected exactly one stable release policy regression step", file=sys.stderr)
    raise SystemExit(1)
step_name = "      - name: Publish bootable image as a GitHub release"
start = workflow.find(step_name)
if start < 0 or workflow.find(step_name, start + len(step_name)) >= 0:
    print("expected exactly one bootable-release publishing step", file=sys.stderr)
    raise SystemExit(1)

end = workflow.find("\n      - name:", start + len(step_name))
if end < 0:
    end = len(workflow)
step = workflow[start:end]
conditions = [
    line.strip()
    for line in step.splitlines()
    if line.lstrip().startswith("if:")
]
expected = "if: github.event_name == 'push' && github.ref == 'refs/heads/main'"
if conditions != [expected]:
    print(
        "unsafe bootable-release condition; expected exactly "
        f"{expected!r}, got {conditions!r}",
        file=sys.stderr,
    )
    raise SystemExit(1)

print("release publication guard regression check passed")

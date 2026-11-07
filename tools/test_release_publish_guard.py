#!/usr/bin/env python3
"""Regression tests for main-only publishing and fail-safe stable ISO updates."""
from pathlib import Path
import subprocess
import sys
import textwrap

workflow = Path(".github/workflows/build.yml").read_text(encoding="utf-8")

# Main pushes share one publisher slot. Do not cancel an active release update:
# it may be in the middle of the stable-asset rollback transaction.
workflow_header = workflow.split("\njobs:", 1)[0]
expected_concurrency = (
    "concurrency:\n"
    "  group: zeroos-build-${{ github.ref }}\n"
    "  cancel-in-progress: false"
)
if expected_concurrency not in workflow_header:
    print("release publisher must be serialized per ref without cancelling an active update", file=sys.stderr)
    raise SystemExit(1)

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

# Verify the immutable commit-specific asset before changing the mutable alias.
versioned_upload = 'gh release upload "$tag" "zeroos-${sha}.iso" --clobber'
versioned_download = 'gh release download "$tag" --pattern "zeroos-${sha}.iso" --dir build/release-versioned-verify --clobber'
versioned_compare = 'cmp --silent build-production/zeroos.iso "build/release-versioned-verify/zeroos-${sha}.iso"'
latest_main_fetch = "git fetch --no-tags origin main"
latest_main_check = 'latest_main_sha="$(git rev-parse FETCH_HEAD)"'
stale_main_condition = 'if ! git diff --quiet "$GITHUB_SHA" "$latest_main_sha" -- . ':(exclude,glob)docs/**' ':(exclude,glob)**/*.md'; then'
stale_main_message = 'echo "ZEROOS: skipping stable release update because release-relevant files changed between ${GITHUB_SHA} and ${latest_main_sha}."'
docs_only_message = 'echo "ZEROOS: main advanced only through documentation files; publishing boot-tested code snapshot ${GITHUB_SHA}."'
stable_backup = 'gh release download "$tag" --pattern "zeroos.iso" --dir build/release-backup --clobber'
stable_upload = 'if ! gh release upload "$tag" zeroos.iso --clobber; then'
stable_download = 'if ! gh release download "$tag" --pattern "zeroos.iso" --dir build/release-verify --clobber; then'
stable_compare = 'if ! cmp --silent build-production/zeroos.iso build/release-verify/zeroos.iso; then'

required = (
    versioned_upload,
    versioned_download,
    versioned_compare,
    stable_backup,
    stable_upload,
    stable_download,
    stable_compare,
)
missing = [check for check in required if check not in step]
if missing:
    print(f"release publisher is missing required integrity/rollback checks: {missing!r}", file=sys.stderr)
    raise SystemExit(1)

stale_fetch_pos = step.find(latest_main_fetch)
stale_check_pos = step.find(latest_main_check)
stale_condition_pos = step.find(stale_main_condition)
stable_backup_pos = step.find(stable_backup)
if not (
    step.find(versioned_compare) < stale_fetch_pos < stale_check_pos <
    stale_condition_pos < stable_backup_pos
):
    print("release freshness check must run after versioned verification and before stable-alias mutation", file=sys.stderr)
    raise SystemExit(1)
stale_guard_block = step[stale_fetch_pos:stable_backup_pos]
if stale_main_message not in stale_guard_block or docs_only_message not in stale_guard_block or "exit 0" not in stale_guard_block:
    print("release guard must skip code-stale runs but permit documentation-only main advances", file=sys.stderr)
    raise SystemExit(1)

# Exercise the exact Git pathspec used by the workflow. Documentation-only
# successors are compatible with the boot-tested code snapshot; kernel or
# other release-relevant changes must keep the stale-run guard closed.
def git(repo_path, *args):
    return subprocess.run(
        ["git", *args], cwd=repo_path, check=True,
        text=True, capture_output=True,
    ).stdout.strip()

with __import__("tempfile").TemporaryDirectory() as temp:
    from pathlib import Path as _Path
    root = _Path(temp)
    git(root, "init", "-q")
    git(root, "config", "user.name", "ZEROOS regression")
    git(root, "config", "user.email", "zeroos-regression@example.invalid")
    (root / "kernel").mkdir()
    (root / "kernel" / "memory.c").write_text("baseline\\n", encoding="utf-8")
    git(root, "add", ".")
    git(root, "commit", "-qm", "baseline")
    base_sha = git(root, "rev-parse", "HEAD")

    (root / "docs" / "ZEROOS_MASTER").mkdir(parents=True)
    (root / "docs" / "ZEROOS_MASTER" / "VALIDATION.md").write_text("docs update\\n", encoding="utf-8")
    (root / "README.md").write_text("readme update\\n", encoding="utf-8")
    git(root, "add", ".")
    git(root, "commit", "-qm", "documentation only")
    docs_sha = git(root, "rev-parse", "HEAD")
    pathspec = [
        "git", "diff", "--quiet", base_sha, docs_sha, "--", ".",
        ":(exclude,glob)docs/**", ":(exclude,glob)**/*.md",
    ]
    docs_result = subprocess.run(pathspec, cwd=root)
    if docs_result.returncode != 0:
        print("documentation-only commits must not make a boot-tested code snapshot stale", file=sys.stderr)
        raise SystemExit(1)

    (root / "kernel" / "memory.c").write_text("kernel change\\n", encoding="utf-8")
    git(root, "add", ".")
    git(root, "commit", "-qm", "kernel change")
    code_sha = git(root, "rev-parse", "HEAD")
    code_result = subprocess.run(
        ["git", "diff", "--quiet", base_sha, code_sha, "--", ".",
         ":(exclude,glob)docs/**", ":(exclude,glob)**/*.md"],
        cwd=root,
    )
    if code_result.returncode != 1:
        print("release-relevant kernel changes must keep the stale-run guard closed", file=sys.stderr)
        raise SystemExit(1)

positions = [step.find(check) for check in required]
if positions != sorted(positions) or len(set(positions)) != len(positions):
    print(
        "release order must be: upload and verify versioned ISO, back up stable "
        "alias, update stable alias, then download and compare it",
        file=sys.stderr,
    )
    raise SystemExit(1)

rollback_start = step.find("rollback_stable() {")
rollback_end = step.find("\n          }\n", rollback_start)
if rollback_start < 0 or rollback_end < 0:
    print("stable ISO rollback function is missing or malformed", file=sys.stderr)
    raise SystemExit(1)
rollback = step[rollback_start:rollback_end]
rollback_checks = (
    'if [ "$had_stable" -eq 1 ]; then',
    'gh release upload "$tag" zeroos.iso --clobber || {',
    "cmp --silent build/release-backup/zeroos.previous.iso build/release-rollback-verify/zeroos.iso",
    "assets_now=",
    'gh release delete-asset "$tag" zeroos.iso --yes',
)
missing_rollback = [check for check in rollback_checks if check not in rollback]
if missing_rollback:
    print(f"rollback does not safely restore/remove the prior alias: {missing_rollback!r}", file=sys.stderr)
    raise SystemExit(1)

if step.count("rollback_stable || exit 1") != 3:
    print("upload, download, and comparison failures must all invoke stable ISO rollback", file=sys.stderr)
    raise SystemExit(1)

# Release metadata must not claim success before the mutable ISO alias is verified.
pending_notes = 'pending_notes="Release publication is in progress; stable ISO verification has not completed."'
create_pending = 'gh release create "$tag" "zeroos-${sha}.iso" --title "$title" --notes "$pending_notes"'
release_edit = 'gh release edit "$tag" --notes "$notes"'
stable_verified = '# The byte comparison above succeeded; only now may release notes describe this build.'
if pending_notes not in step or create_pending not in step:
    print("new releases must start with honest pending-verification notes", file=sys.stderr)
    raise SystemExit(1)
if step.count(release_edit) != 1:
    print("final release notes must be updated exactly once", file=sys.stderr)
    raise SystemExit(1)
if stable_verified not in step or step.find(release_edit) < step.find(stable_verified):
    print("final release notes must be updated only after stable ISO verification succeeds", file=sys.stderr)
    raise SystemExit(1)

# The release step is skipped on PR events, so parse its shell explicitly here.
run_marker = "        run: |\n"
if run_marker not in step:
    print("release step has no inline shell script to syntax-check", file=sys.stderr)
    raise SystemExit(1)
shell_script = textwrap.dedent(step.split(run_marker, 1)[1])
syntax = subprocess.run(["bash", "-n"], input=shell_script, text=True, capture_output=True)
if syntax.returncode != 0:
    print(f"release publishing shell syntax is invalid: {syntax.stderr}", file=sys.stderr)
    raise SystemExit(syntax.returncode)

print("release publication guard, rollback, and shell-syntax regression checks passed")

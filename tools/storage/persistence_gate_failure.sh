#!/usr/bin/env bash
# Preserve the failing command's status while printing storage-gate diagnostics.
# This function is installed as the ERR trap by the GitHub Actions persistence step.
storage_persistence_gate_failure() {
    local original_status="${1:-1}"
    case "$original_status" in
        ''|*[!0-9]*|0) original_status=1 ;;
    esac

    # Disable recursive ERR handling and let diagnostics run even when a log
    # is absent. The original failure remains authoritative.
    trap - ERR
    set +e

    echo "storage persistence gate failed (status=$original_status); missing patterns + log ends:" >&2
    for pattern in \
        "storage discovery: sata_disks=1 nvme_namespaces=1." \
        "storage driver test sata0 destructive" \
        "storage driver test nvme0n1 destructive" \
        "vfs: mounted sata0p1" \
        "storage persistence: /data boot counter" \
        "storage persistence check passed" \
        "storage userspace probe wrote" \
        "storage Stage 3 certification complete." \
        "ZEROOS PANIC:|FAILED"; do
        grep -qE "$pattern" build/serial-disk1.log 2>/dev/null || echo "MISSING boot1: $pattern" >&2
        grep -qE "$pattern" build/serial-disk2.log 2>/dev/null || echo "MISSING boot2: $pattern" >&2
    done

    for file in build/serial-disk1.log build/serial-disk2.log; do
        if test -f "$file"; then
            echo "END of $file" >&2
            tail -c 1800 "$file" | tr "\r\n" " " >&2
            echo >&2
        fi
    done

    exit "$original_status"
}

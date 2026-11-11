#!/usr/bin/env bash
# Preserve the failing QEMU gate command status while retaining serial context.
zeroos_boot_gate_failure() {
    local original_status="${1:-1}"
    local serial_log="${2:-build/serial.log}"
    case "$original_status" in
        ''|*[!0-9]*|0) original_status=1 ;;
    esac

    trap - ERR
    set +e
    echo "boot milestone failed (status=$original_status); log END follows first" >&2
    if test -f "$serial_log"; then
        serial_end="$(tail -c 1800 "$serial_log" | tr "\\r\\n" " ")"
        echo "::error title=serial end::$serial_end" >&2
        echo "boot milestone failed; serial tail follows" >&2
        tail -c 6000 "$serial_log" | tr "\\r\\n" " " >&2
        echo >&2
    else
        echo "serial log not produced: $serial_log" >&2
    fi
    exit "$original_status"
}

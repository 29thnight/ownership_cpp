#!/usr/bin/env bash
# Turns the failing part of a test log into GitHub Actions error annotations,
# which stay readable through the checks API even when raw logs are not.
# Usage: scripts/ci_annotate.sh <log file> [title]
set -uo pipefail
log=${1:?log file}
title=${2:-test failure}
[[ -f "$log" ]] || { echo "::error title=$title::no log at $log"; exit 0; }

encode() { # GitHub workflow-command escaping for a multi-line message
    local text
    text=$(cat)
    text=${text//'%'/'%25'}
    text=${text//$'\r'/'%0D'}
    text=${text//$'\n'/'%0A'}
    printf '%s' "$text"
}

# Each failure line with the context after it, at most 8 blocks of 40 lines.
grep -n -E '^FAIL|\*\*\*Failed|\*\*\*Exception|Subprocess aborted|error:|error C[0-9]+|fatal error|CMake Error|Aborted|Segmentation fault|ERROR: (Address|Leak|Thread)Sanitizer|runtime error:' "$log" \
    | cut -d: -f1 | head -8 | while read -r line; do
        sed -n "$((line > 3 ? line - 3 : 1)),$((line + 36))p" "$log" | encode | {
            read -r -d '' message
            echo "::error title=$title (log line $line)::$message"
        }
    done
# Always include the end of the log, where runners print their summaries.
tail -n 25 "$log" | encode | { read -r -d '' message; echo "::error title=$title (log tail)::$message"; }
exit 0

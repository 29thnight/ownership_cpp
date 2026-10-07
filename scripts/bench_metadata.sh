#!/usr/bin/env bash
# Prints the seven measurement fields for a benchmark run.
# Usage: bench_metadata.sh <compiler> <header-include-dir> <source> <workload> <arguments> <flags...>
set -euo pipefail
cxx=$1 include=$2 source=$3 workload=$4 arguments=$5
shift 5
lscpu_field() { lscpu 2>/dev/null | awk -F: -v key="$1" '$1 == key { gsub(/^[ \t]+/, "", $2); print $2; exit }'; }
printf 'UTC timestamp: '; date -u +%FT%TZ
printf '1. CPU model: %s\n' "$(lscpu_field 'Model name' || true)"
printf '   Architecture: %s; OS: %s\n' "$(uname -m)" "$(uname -s -r)"
printf '2. Cores: %s online; sockets %s; NUMA nodes %s\n' \
    "$(nproc)" "$(lscpu_field 'Socket(s)' || true)" "$(lscpu_field 'NUMA node(s)' || true)"
printf '3. Frequency: nominal %s MHz (cpuinfo); governor %s; turbo/DVFS not controlled\n' \
    "$(awk -F': ' '/cpu MHz/ { print $2; exit }' /proc/cpuinfo 2>/dev/null || true)" \
    "$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo unavailable)"
printf '   SMT: %s thread(s) per core\n' "$(lscpu_field 'Thread(s) per core' || true)"
printf '4. Compiler: %s\n' "$("$cxx" --version | head -n 1)"
printf '   Flags: '; printf '%q ' "$@" -I"$include"; printf '\n'
printf '5. Workload: %s\n' "$workload"
printf '6. Baseline: std::shared_ptr/std::weak_ptr from the same standard library, same process\n'
printf '7. Method: steady_clock, discarded warmups, seeded shuffled case order, median/min/p90/cv\n'
printf 'Arguments: %s\n' "$arguments"
printf 'ownership.hpp SHA256: '; sha256sum "$include/own/ownership.hpp" | cut -d ' ' -f 1
printf '%s SHA256: ' "$(basename "$source")"; sha256sum "$source" | cut -d ' ' -f 1

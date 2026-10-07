#!/usr/bin/env bash
# Reference-count contention scaling benchmark.
#   scripts/benchmark_scaling.sh             full run (15 samples, 2 warmups)
#   QUICK=1 scripts/benchmark_scaling.sh     smoke test, a few seconds
# Override CXX, BENCH_CXXFLAGS, BENCH_OUTPUT, OWN_INCLUDE (header directory to
# measure, for before/after comparisons), and pass extra harness arguments
# such as --samples, --iterations or --max-threads.
set -euo pipefail
cd "$(dirname "$0")/.."
CXX="${CXX:-g++}"
read -r -a flags <<< "${BENCH_CXXFLAGS:--std=c++20 -O2 -DNDEBUG -pthread -Wall -Wextra -Wpedantic}"
include="${OWN_INCLUDE:-include}"
output="${BENCH_OUTPUT:-benchmarks/results/scaling_$(date -u +%Y%m%dT%H%M%SZ)}"
if [[ -e "$output/raw.csv" ]]; then
    printf 'Refusing to overwrite existing benchmark samples: %s\n' "$output" >&2
    exit 1
fi
args=("$@")
if [[ "${QUICK:-0}" == 1 ]]; then
    args=(--samples 3 --warmups 1 --iterations 20000 "${args[@]}")
fi
mkdir -p benchmarks/.build "$output"
binary="benchmarks/.build/refcount_scaling"
"$CXX" "${flags[@]}" -I"$include" benchmarks/refcount_scaling.cpp -o "$binary"
lscpu_field() { lscpu 2>/dev/null | awk -F: -v key="$1" '$1 == key { gsub(/^[ \t]+/, "", $2); print $2; exit }'; }
{
    printf 'UTC timestamp: '; date -u +%FT%TZ
    printf '1. CPU model: %s\n' "$(lscpu_field 'Model name' || true)"
    printf '   Architecture: %s; OS: %s\n' "$(uname -m)" "$(uname -s -r)"
    printf '2. Cores: %s online; sockets %s; NUMA nodes %s; no pinning applied\n' \
        "$(nproc)" "$(lscpu_field 'Socket(s)' || true)" "$(lscpu_field 'NUMA node(s)' || true)"
    printf '3. Frequency: nominal %s MHz (cpuinfo); governor %s; turbo/DVFS not controlled\n' \
        "$(awk -F': ' '/cpu MHz/ { print $2; exit }' /proc/cpuinfo 2>/dev/null || true)" \
        "$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo unavailable)"
    printf '   SMT: %s thread(s) per core\n' "$(lscpu_field 'Thread(s) per core' || true)"
    printf '4. Compiler: %s\n' "$("$CXX" --version | head -n 1)"
    printf '   Flags: '; printf '%q ' "${flags[@]}" -I"$include"; printf '\n'
    printf '5. Workload: copy+drop of a shared owner (shared line), per-thread private owners, weak lock+drop\n'
    printf '6. Baseline: std::shared_ptr/std::weak_ptr from the same standard library\n'
    printf '7. Method: steady_clock, slowest thread per sample, discarded warmups, median/min/p90/cv\n'
    printf 'Arguments: %s\n' "${args[*]:-defaults}"
    printf 'ownership.hpp SHA256: '; sha256sum "$include/own/ownership.hpp" | cut -d ' ' -f 1
    printf 'refcount_scaling.cpp SHA256: '; sha256sum benchmarks/refcount_scaling.cpp | cut -d ' ' -f 1
} > "$output/metadata.txt"
"$binary" --output "$output" "${args[@]}" | tee "$output/console.txt"
(cd "$output" && sha256sum console.txt metadata.txt raw.csv summary.csv > SHA256SUMS)
printf '\nResults saved to %s\n' "$output"

#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
CXX="${CXX:-g++}"
# Override BENCH_CXXFLAGS to reproduce on a different compiler or ISA.
read -r -a flags <<< "${BENCH_CXXFLAGS:--std=c++20 -O3 -DNDEBUG -march=native -pthread -Wall -Wextra -Wpedantic}"
output="${BENCH_OUTPUT:-benchmarks/results/$(date -u +%Y%m%dT%H%M%SZ)}"
mkdir -p benchmarks/.build "$output"
binary="benchmarks/.build/ownership_bench"
"$CXX" "${flags[@]}" -Iinclude benchmarks/ownership_bench.cpp -o "$binary"
{
    printf 'UTC timestamp: '; date -u +%FT%TZ
    printf 'Compiler: '; command -v "$CXX"
    "$CXX" --version
    printf 'Build command: '; printf '%q ' "$CXX" "${flags[@]}" -Iinclude benchmarks/ownership_bench.cpp -o "$binary"; printf '\n'
    printf 'Run command: '; printf '%q ' "$binary" --output "$output" "$@"; printf '\n'
    printf 'HEAD: '; git rev-parse HEAD
    printf '\nSource hashes (identify uncommitted implementation):\n'; sha256sum include/own/ownership.hpp benchmarks/ownership_bench.cpp
    printf '\nKernel: '; uname -a
    printf '\nCPU details:\n'; if command -v lscpu >/dev/null; then lscpu 2>&1 || cat /proc/cpuinfo; else cat /proc/cpuinfo; fi
    printf '\nProcess CPU affinity: '; if command -v taskset >/dev/null; then taskset -pc $$; else printf 'not available\n'; fi
    printf '\nCPU cgroup quota:\n'; cat /sys/fs/cgroup/cpu.max 2>/dev/null || true
    printf '\nCPU governor (if visible):\n'; cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || true
    printf '\nRuntime libraries:\n'; ldd "$binary" || true
    printf '\nHost load before run:\n'; uptime
} > "$output/metadata.txt"
"$binary" --output "$output" "$@" | tee "$output/console.txt"
printf '\nResults saved to %s\n' "$output"

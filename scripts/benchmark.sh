#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
CXX="${CXX:-g++}"
# Override BENCH_CXXFLAGS to reproduce on a different compiler or ISA.
read -r -a flags <<< "${BENCH_CXXFLAGS:--std=c++20 -O3 -DNDEBUG -march=native -pthread -Wall -Wextra -Wpedantic}"
output="${BENCH_OUTPUT:-benchmarks/results/$(date -u +%Y%m%dT%H%M%SZ)}"
source="${BENCH_SOURCE:-benchmarks/ownership_bench.cpp}"
if [[ -e "$output/raw.csv" ]]; then
    printf 'Refusing to overwrite existing benchmark samples: %s\n' "$output" >&2
    exit 1
fi
mkdir -p benchmarks/.build "$output"
binary="benchmarks/.build/ownership_bench"
"$CXX" "${flags[@]}" -Iinclude "$source" -o "$binary"
{
    printf 'UTC timestamp: '; date -u +%FT%TZ
    printf 'Public reproduction metadata; workspace paths and host identifiers omitted.\n'
    "$CXX" --version | head -n 1
    printf 'Compiler flags: '; printf '%q ' "${flags[@]}" -Iinclude; printf '\n'
    printf 'Architecture: '; uname -m
    printf 'OS: '; uname -s -r
    printf 'CPU model: '; awk -F ': ' '/model name/ { print $2; exit }' /proc/cpuinfo 2>/dev/null || printf 'unavailable\n'
    printf 'ownership.hpp SHA256: '; sha256sum include/own/ownership.hpp | cut -d ' ' -f 1
    printf 'ownership_bench.cpp SHA256: '; sha256sum "$source" | cut -d ' ' -f 1
} > "$output/metadata.txt"
"$binary" --output "$output" "$@" | tee "$output/console.txt"
cat "$output/run.txt" >> "$output/metadata.txt"
(cd "$output" && sha256sum allocations.csv console.txt lifetime_validation.txt metadata.txt raw.csv run.txt sizes.csv summary.csv > SHA256SUMS)
printf '\nResults saved to %s\n' "$output"

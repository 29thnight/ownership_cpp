#!/usr/bin/env bash
# Allocator-aware exclusive ownership: 5-word handle versus header-in-allocation
# prototype (measurement only; the library is unchanged).
#   scripts/benchmark_allocated_unique.sh            full run
#   QUICK=1 scripts/benchmark_allocated_unique.sh    smoke test
# Single-threaded; pinned to the first allowed CPU when taskset is available.
set -euo pipefail
cd "$(dirname "$0")/.."
CXX="${CXX:-g++}"
read -r -a flags <<< "${BENCH_CXXFLAGS:--std=c++20 -O2 -DNDEBUG -pthread -Wall -Wextra -Wpedantic}"
include="${OWN_INCLUDE:-include}"
source=benchmarks/allocated_unique_layout.cpp
output="${BENCH_OUTPUT:-benchmarks/results/allocated_unique_$(date -u +%Y%m%dT%H%M%SZ)}"
if [[ -e "$output/raw.csv" ]]; then
    printf 'Refusing to overwrite existing benchmark samples: %s\n' "$output" >&2
    exit 1
fi
args=("$@")
if [[ "${QUICK:-0}" == 1 ]]; then args=(--samples 3 --warmups 1 --scale 20 "${args[@]}"); fi
mkdir -p benchmarks/.build "$output"
binary=benchmarks/.build/allocated_unique_layout
"$CXX" "${flags[@]}" -I"$include" "$source" -o "$binary"
runner=()
if command -v taskset >/dev/null; then
    runner=(taskset -c "$(taskset -cp $$ | awk -F': ' '{ split($2, a, /[,-]/); print a[1] }')")
fi
scripts/bench_metadata.sh "$CXX" "$include" "$source" \
    "create/read/destroy; one owner moved through 64 slots; 4,096 owners moved between vectors; payload reads through 4,096 owners. Runner: ${runner[*]:-unpinned}" \
    "${args[*]:-defaults}" "${flags[@]}" > "$output/metadata.txt"
"${runner[@]}" "$binary" --output "$output" "${args[@]}" | tee "$output/console.txt"
(cd "$output" && sha256sum console.txt metadata.txt raw.csv summary.csv > SHA256SUMS)
printf '\nResults saved to %s\n' "$output"

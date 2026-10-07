#!/usr/bin/env bash
# Control-block layout benchmark: footprint, create/destroy, scans, line sharing.
#   scripts/benchmark_layout.sh             full run
#   QUICK=1 scripts/benchmark_layout.sh     smoke test, a few seconds
# Override CXX, BENCH_CXXFLAGS, BENCH_OUTPUT and OWN_INCLUDE (header directory to
# measure, for before/after comparisons). Extra arguments go to the harness.
set -euo pipefail
cd "$(dirname "$0")/.."
CXX="${CXX:-g++}"
read -r -a flags <<< "${BENCH_CXXFLAGS:--std=c++20 -O2 -DNDEBUG -pthread -Wall -Wextra -Wpedantic}"
include="${OWN_INCLUDE:-include}"
source=benchmarks/control_block_layout.cpp
output="${BENCH_OUTPUT:-benchmarks/results/layout_$(date -u +%Y%m%dT%H%M%SZ)}"
if [[ -e "$output/raw.csv" ]]; then
    printf 'Refusing to overwrite existing benchmark samples: %s\n' "$output" >&2
    exit 1
fi
args=("$@")
if [[ "${QUICK:-0}" == 1 ]]; then args=(--samples 3 --warmups 1 --scale 20 "${args[@]}"); fi
mkdir -p benchmarks/.build "$output"
binary=benchmarks/.build/control_block_layout
"$CXX" "${flags[@]}" -I"$include" "$source" -o "$binary"
scripts/bench_metadata.sh "$CXX" "$include" "$source" \
    "make_shared create/read/destroy; shuffled payload reads over 16k and 256k owners; one reader thread beside copier threads on one object" \
    "${args[*]:-defaults}" "${flags[@]}" > "$output/metadata.txt"
"$binary" --output "$output" "${args[@]}" | tee "$output/console.txt"
(cd "$output" && sha256sum console.txt metadata.txt raw.csv sizes.csv summary.csv > SHA256SUMS)
printf '\nResults saved to %s\n' "$output"

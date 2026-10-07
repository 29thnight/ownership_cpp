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
scripts/bench_metadata.sh "$CXX" "$include" benchmarks/refcount_scaling.cpp \
    "copy+drop of a shared owner (shared line), per-thread private owners, weak lock+drop; pinned only with --pin 1" \
    "${args[*]:-defaults}" "${flags[@]}" > "$output/metadata.txt"
"$binary" --output "$output" "${args[@]}" | tee "$output/console.txt"
(cd "$output" && sha256sum console.txt metadata.txt raw.csv summary.csv > SHA256SUMS)
printf '\nResults saved to %s\n' "$output"

#!/usr/bin/env bash
# Reproduce the final-candidate precision study. Run tests separately first.
set -euo pipefail
cd "$(dirname "$0")/../.."
CXX="${CXX:-g++}"
read -r -a flags <<< "${UNIQUE_PRECISION_CXXFLAGS:--std=c++20 -O3 -DNDEBUG -march=native -pthread -Wall -Wextra -Wpedantic}"
main_cpu="${UNIQUE_PRECISION_MAIN_CPU:-2}"
worker_cpu="${UNIQUE_PRECISION_WORKER_CPU:-3}"
output="${UNIQUE_PRECISION_OUTPUT:-build/unique_precision_$(date -u +%Y%m%dT%H%M%SZ)}"
expected=17a1fdb0773ae287fc8955eecb629c20f7052c18324d1efd53554d4ed27cc90d
: "${UNIQUE_PRECISION_TESTED_HEADER_SHA256:?Supply the tested current header SHA256 after CPU-intensive tests finish}"
[[ "$UNIQUE_PRECISION_TESTED_HEADER_SHA256" == "$expected" && "$(sha256sum include/own/ownership.hpp | cut -d ' ' -f 1)" == "$expected" ]] || { echo 'Final tested header mismatch' >&2; exit 1; }
[[ "$(sha256sum benchmarks/precision/baseline/include/own/ownership.hpp | cut -d ' ' -f 1)" == 4f13b3edcab3a52d7307fcad05b9da8fdcf7333264e5724eb8107e663445b225 ]] || { echo 'Frozen baseline mismatch' >&2; exit 1; }
[[ ! -e "$output" ]] || { echo 'Refusing to overwrite a result directory' >&2; exit 1; }
mkdir -p "$output"/{baseline,candidate}
for version in baseline candidate; do
 if [[ $version == baseline ]]; then inc=benchmarks/precision/baseline/include; else inc=include; fi
 "$CXX" "${flags[@]}" -I"$inc" benchmarks/precision/driver.cpp -o "$output/$version/precision"
 "$CXX" "${flags[@]}" -DUNIQUE_BENCH_CHECK -I"$inc" benchmarks/precision/driver.cpp -o "$output/$version/check"
 "$output/$version/check" --main-cpu "$main_cpu" --worker-cpu "$worker_cpu" > "$output/$version/checks.csv"
done
{
 "$CXX" --version | head -1
 printf 'flags: '; printf '%q ' "${flags[@]}"; printf '\n'
 printf 'architecture: '; uname -m
 printf 'OS/kernel: '; uname -s -r
 printf 'CPU: '; awk -F ': ' '/model name/ { print $2; exit }' /proc/cpuinfo
 printf 'main CPU: %s; worker CPU: %s\n' "$main_cpu" "$worker_cpu"
 printf '4 processes/header; 31 blocks/case/process; 2 reversed passes; target 25 ms; seeds 20261007 + 7919*i\n'
 sha256sum include/own/ownership.hpp benchmarks/precision/{driver.cpp,workloads.inc,analyze.py,run.sh} benchmarks/precision/baseline/include/own/ownership.hpp
} > "$output/metadata.txt"
for item in baseline:0 candidate:0 candidate:1 baseline:1 baseline:2 candidate:2 candidate:3 baseline:3; do
 version=${item%:*}; i=${item#*:}
 "$output/$version/precision" --rounds 31 --target-ms 25 --run p$i --seed $((20261007+i*7919)) --main-cpu "$main_cpu" --worker-cpu "$worker_cpu" > "$output/$version/p$i.csv" 2> "$output/$version/p$i.log"
done
for version in baseline candidate; do
 UNIQUE_PRECISION_MAIN_CPU="$main_cpu" OPENBLAS_NUM_THREADS=1 python3 benchmarks/precision/analyze.py "$output/$version" > "$output/$version/summary.txt"
done
printf 'Precision study complete: %s\n' "$output"

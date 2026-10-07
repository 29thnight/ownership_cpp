#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
CXX="${CXX:-g++}"
read -r -a flags <<< "${UNIQUE_BENCH_CXXFLAGS:--std=c++20 -O3 -DNDEBUG -march=native -pthread -Wall -Wextra -Wpedantic}"
output="${UNIQUE_BENCH_OUTPUT:-benchmarks/results/unique_$(date -u +%Y%m%dT%H%M%SZ)}"
baseline="${UNIQUE_ERASED_BASELINE:-benchmarks/baseline_unique_erased}"
manifest="${UNIQUE_BASELINE_MANIFEST:-}"
rounds="${UNIQUE_BENCH_ROUNDS:-51}"
warmups="${UNIQUE_BENCH_WARMUPS:-5}"
old_hash=778759120c5b2920c85b653f011444d116f2501e205f528755ccfc31d735cd28
new_hash=$(sha256sum include/own/ownership.hpp | cut -d ' ' -f 1)
: "${UNIQUE_BENCH_TESTED_HEADER_SHA256:?Set to the header SHA256 only after all desired tests finish; do not run CPU tests concurrently}"
[[ "$UNIQUE_BENCH_TESTED_HEADER_SHA256" == "$new_hash" ]] || { echo 'Tested header hash does not match.' >&2; exit 1; }
[[ "$(sha256sum "$baseline/include/own/ownership.hpp" | cut -d ' ' -f 1)" == "$old_hash" ]] || {
    echo 'Frozen prototype header does not match the required source hash.' >&2; exit 1;
}
# Frozen sources are read-only inputs; all products go into the current checkout.
verify_baseline() {
    if [[ -f "$manifest" ]]; then
        python3 - "$baseline" "$manifest" <<'PY'
import hashlib, json, pathlib, sys
base = pathlib.Path(sys.argv[1])
rows = json.loads(pathlib.Path(sys.argv[2]).read_text())
for row in rows:
    assert hashlib.sha256((base / row['path']).read_bytes()).hexdigest() == row['sha256'], 'frozen baseline changed'
print(f'Frozen baseline manifest: {len(rows)} files verified')
PY
    else
        printf 'Frozen baseline manifest: unavailable; required header SHA256 verified\n'
    fi
}
# Every saved timing set is immutable; pick another directory for a rerun.
if [[ -e "$output/raw.csv" || -e "$output/metadata.txt" ]]; then
    printf 'Refusing to overwrite existing unique benchmark results.\n' >&2
    exit 1
fi
mkdir -p benchmarks/.build "$output"
build=benchmarks/.build/unique_compare
mkdir -p "$build"
for version in frozen_erased lean; do
    if [[ "$version" == frozen_erased ]]; then include="$baseline/include"; capability=0;
    else include=include; capability=1; fi
    "$CXX" "${flags[@]}" -DOWN_BENCH_LEAN="$capability" -I"$include" benchmarks/unique_bench.cpp -o "$build/$version"
    "$CXX" "${flags[@]}" -DOWN_BENCH_LEAN="$capability" -DUNIQUE_BENCH_CHECK -I"$include" benchmarks/unique_bench.cpp -o "$build/${version}_check"
done
"$build/frozen_erased_check" > "$output/checks.csv"
"$build/lean_check" --no-header >> "$output/checks.csv"
{
    printf 'Public reproduction metadata only.\n'
    printf 'UTC timestamp: '; date -u +%FT%TZ
    "$CXX" --version | head -n 1
    printf 'Timing compiler flags: '; printf '%q ' "${flags[@]}"; printf '\n'
    printf 'Build selectors: OWN_BENCH_LEAN=0 frozen prototype; OWN_BENCH_LEAN=1 lean header\n'
    printf 'Diagnostic flags: timing flags plus -DUNIQUE_BENCH_CHECK\n'
    printf 'Architecture: '; uname -m
    printf 'OS kernel: '; uname -s -r
    printf 'OS version: '; sed -n 's/^PRETTY_NAME=//p' /etc/os-release | tr -d '"'
    printf 'CPU model: '; awk -F ': ' '/model name/ { print $2; exit }' /proc/cpuinfo
    printf 'Available logical CPUs: '; nproc
    printf 'Samples per implementation/case/process: %s\nWarmups per implementation/case/process: %s\n' "$rounds" "$warmups"
    printf 'Independent processes per header build: 2\nOrder seeds: primary=20261007 repeat=987654321\n'
    printf 'Process schedule: frozen-primary, lean-primary, lean-repeat, frozen-repeat\n'
    printf 'Work counts: create=50000 observable_move=500000 borrow=1000000 frames=64 vector_batches=64 jobs=2048\n'
    printf 'Payload=64 bytes; frame=256 resources/4096 views; vector batch=1024 transferred owners; queue capacity=64\n'
    printf 'Object allocations are included except retained move/borrow roots and vector-transfer resources.\n'
    printf 'Vector capacity and worker-thread creation are outside timing. No pool or global allocation replacement.\n'
    printf 'Test-complete header supplied by operator: %s\n' "$UNIQUE_BENCH_TESTED_HEADER_SHA256"
    printf 'frozen ownership.hpp SHA256: %s\nlean ownership.hpp SHA256: %s\n' "$old_hash" "$new_hash"
    printf 'unique_bench.cpp SHA256: '; sha256sum benchmarks/unique_bench.cpp | cut -d ' ' -f 1
    printf 'benchmark_unique.sh SHA256: '; sha256sum scripts/benchmark_unique.sh | cut -d ' ' -f 1
    verify_baseline
} > "$output/metadata.txt"
# Save only the two tiny, untimed functions relevant to move-elision claims.
python3 - "$build" "$output/codegen.txt" <<'PY'
import pathlib, re, subprocess, sys
out = []
for version in ['frozen_erased', 'lean']:
    disassembly = subprocess.check_output(['objdump', '-d', '--no-show-raw-insn', str(pathlib.Path(sys.argv[1]) / version)], text=True)
    out.append(f'BUILD {version}\n')
    for symbol in ['bench_unobserved_own_moves', 'bench_unobserved_std_moves']:
        match = re.search(r'^[0-9a-f]+ <' + symbol + r'>:\n(.*?)(?=\n\n)', disassembly, re.M | re.S)
        assert match, symbol
        # Function-relative addresses avoid recording unrelated binary layout.
        body = match.group(1).splitlines()
        first = int(body[0].split(':')[0].strip(), 16)
        out.append(symbol + ':\n')
        for line in body:
            address, instruction = line.split(':', 1)
            out.append(f'  +{int(address.strip(), 16)-first:02x}: {instruction.strip()}\n')
        out.append('\n')
pathlib.Path(sys.argv[2]).write_text(''.join(out))
PY
"$build/frozen_erased" --run primary --rounds "$rounds" --warmups "$warmups" --seed 20261007 > "$output/raw.csv"
"$build/lean" --run primary --rounds "$rounds" --warmups "$warmups" --seed 20261007 --no-header >> "$output/raw.csv"
"$build/lean" --run repeat --rounds "$rounds" --warmups "$warmups" --seed 987654321 --no-header >> "$output/raw.csv"
"$build/frozen_erased" --run repeat --rounds "$rounds" --warmups "$warmups" --seed 987654321 --no-header >> "$output/raw.csv"
[[ "$(sha256sum include/own/ownership.hpp | cut -d ' ' -f 1)" == "$new_hash" ]] || { echo 'Lean header changed during benchmarking.' >&2; exit 1; }
verify_baseline
python3 - "$output" "$rounds" <<'PY'
import csv, math, pathlib, statistics, sys
out, rounds = pathlib.Path(sys.argv[1]), int(sys.argv[2])
rows = list(csv.DictReader((out / 'raw.csv').open()))
cases = ['create_read_destroy', 'observable_move_roundtrip', 'borrow_read_loop', 'frame_draw_batch', 'vector_owner_transfer', 'synchronized_job_handoff']
impls = {'frozen_erased': ['std_unique_8', 'std_erased_unique_40', 'own_old_unique_40'],
         'lean': ['std_unique_8', 'std_erased_unique_40', 'own_unique_8', 'own_allocated_unique_40']}
assert len(rows) == rounds * len(cases) * sum(map(len, impls.values())) * 2
keys = {(r['build'], r['run'], r['round'], r['case'], r['implementation']) for r in rows}
assert len(keys) == len(rows), 'duplicate sample'
assert all(math.isfinite(float(r['ns_per_operation'])) and float(r['total_ns']) > 0 for r in rows)
for case in cases:
    group = [r for r in rows if r['case'] == case]
    assert len({r['checksum'] for r in group}) == 1, 'cross-build checksum mismatch'
    assert len({r['operations'] for r in group}) == 1
for build in impls:
    for run in ['primary', 'repeat']:
        for rnd in range(rounds):
            group = [r for r in rows if r['build'] == build and r['run'] == run and int(r['round']) == rnd]
            assert sorted(int(r['order']) for r in group) == list(range(len(cases) * len(impls[build])))
            for case in cases:
                selected = [r for r in group if r['case'] == case]
                assert {r['implementation'] for r in selected} == set(impls[build])
                assert max(int(r['order']) for r in selected) - min(int(r['order']) for r in selected) == len(impls[build]) - 1
checks = list(csv.DictReader((out / 'checks.csv').open()))
assert len(checks) == len(cases) * sum(map(len, impls.values()))
for r in checks:
    assert r['constructed'] == r['destroyed']
    assert int(r['borrow_bytes']) == 8 and int(r['payload_bytes']) == 64
    assert int(r['handle_bytes']) == (8 if r['implementation'] in ['std_unique_8', 'own_unique_8'] else 40)
for case in cases:
    assert len({r['checksum'] for r in checks if r['case'] == case}) == 1

def quantile(xs, q):
    xs = sorted(xs)
    at = (len(xs) - 1) * q
    lo, hi = math.floor(at), math.ceil(at)
    return xs[lo] + (xs[hi] - xs[lo]) * (at - lo)

with (out / 'summary.csv').open('w', newline='') as f:
    writer = csv.writer(f)
    writer.writerow(['build', 'run', 'case', 'implementation', 'samples', 'median_ns_per_operation',
                     'p10_ns_per_operation', 'p90_ns_per_operation', 'p95_ns_per_operation', 'p99_ns_per_operation',
                     'paired_ratio_vs_std_unique_8', 'paired_ratio_vs_std_erased_unique_40'])
    for build in impls:
        for run in ['primary', 'repeat']:
            for case in cases:
                selected = [r for r in rows if r['build'] == build and r['run'] == run and r['case'] == case]
                by_key = {(r['round'], r['implementation']): float(r['ns_per_operation']) for r in selected}
                for impl in impls[build]:
                    samples = [float(r['ns_per_operation']) for r in selected if r['implementation'] == impl]
                    assert len(samples) == rounds
                    ratios = [[by_key[(str(rnd), impl)] / by_key[(str(rnd), baseline)] for rnd in range(rounds)]
                              for baseline in impls[build][:2]]
                    writer.writerow([build, run, case, impl, len(samples), f'{statistics.median(samples):.6f}',
                                     f'{quantile(samples, .1):.6f}', f'{quantile(samples, .9):.6f}',
                                     f'{quantile(samples, .95):.6f}', f'{quantile(samples, .99):.6f}',
                                     *[f'{statistics.median(rs):.6f}' for rs in ratios]])
print(f'Verified {len(rows)} timed samples, {len(checks)} diagnostic rows, cross-build checksums and paired order')
PY
(cd "$output" && sha256sum checks.csv codegen.txt metadata.txt raw.csv summary.csv > SHA256SUMS)
printf 'Unique comparison complete: %s\n' "$output"

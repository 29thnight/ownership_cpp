# Reference-count contention scaling

**Claim.** A shared-owner copy/drop is two atomic read-modify-writes on one
control-block cache line. When several cores copy the same owner, the cost is set
by how often that line moves between cores, so an increment that needs one locked
instruction scales better than a load followed by a compare/exchange retry loop.

**Change measured.** `detail::increment` went from a relaxed load + `lock cmpxchg`
retry loop to one `lock xadd` (`fetch_add`) with the previous value checked
afterward against zero and a saturation threshold at half the counter range.
Weak locking (`try_add_strong`) still needs a compare/exchange, because it must
not increment a zero count, and is the unchanged control.

## Machine and method (seven fields)

1. **CPU:** Intel Xeon Processor @ 2.30GHz (cloud VM, x86-64), Linux 6.18.44
2. **Cores used:** 1, 2 and 4 of 4 online vCPUs, one socket, one NUMA node, no pinning
3. **Frequency:** 2300 MHz nominal per `/proc/cpuinfo`; governor and turbo not
   visible or controlled from the guest. **SMT:** 1 thread per core as reported
4. **Compiler:** GCC 13.3.0, `-std=c++20 -O2 -DNDEBUG -pthread -Wall -Wextra -Wpedantic`
5. **Workload:** `shared_copy_drop`: all threads copy and drop one shared owner
   (one hot line). `private_copy_drop`: each thread copies its own owner (no
   sharing; payloads padded to separate lines). `weak_lock_drop`: all threads lock
   and drop one weak observer
6. **Baseline:** `std::shared_ptr`/`std::weak_ptr` from the same libstdc++, run in
   the same process and interleaved in the same rounds
7. **Method:** `steady_clock`; threads released together on a flag; each sample is
   the slowest thread's time divided by 200,000 pairs (wall time per pair with all
   threads active). 2 discarded warmups, 15 measured samples, seeded shuffled case
   order per round. Median reported with min, p90 and coefficient of variation.
   Every sample checks the payload checksum and that the count returns to one

Each header was run in two independent processes (primary and repeat).

## Results

Median ns per copy/drop (or lock/drop) pair, primary / repeat:

| Case | Threads | own, CAS increment | own, fetch_add increment | std::shared_ptr (CAS run / fetch_add run) |
|---|---:|---:|---:|---:|
| shared_copy_drop | 1 | 13.80 / 12.91 | 12.86 / 12.85 | 16.89 / 16.54, 16.56 / 16.82 |
| shared_copy_drop | 2 | 156.47 / 141.88 | **96.45 / 91.11** | 150.11 / 145.63, 155.74 / 142.43 |
| shared_copy_drop | 4 | 367.72 / 321.05 | **206.42 / 192.89** | 326.83 / 327.04, 373.39 / 342.53 |
| private_copy_drop | 4 | 13.33 / 13.52 | 12.81 / 13.02 | 16.78 / 16.63, 16.67 / 16.59 |
| weak_lock_drop | 2 | 162.99 / 148.67 | 163.67 / 153.54 | 173.42 / 153.88, 176.37 / 168.02 |
| weak_lock_drop | 4 | 341.34 / 346.31 | 380.27 / 311.12 | 356.23 / 348.21, 408.03 / 363.60 |

Full tables with min, p90 and cv for every thread count:
[CAS primary](../benchmarks/results/scaling_cas_increment_primary/summary.csv),
[CAS repeat](../benchmarks/results/scaling_cas_increment_repeat/summary.csv),
[fetch_add primary](../benchmarks/results/scaling_fetch_add_increment_primary/summary.csv),
[fetch_add repeat](../benchmarks/results/scaling_fetch_add_increment_repeat/summary.csv),
each with `raw.csv`, `metadata.txt` and `console.txt`. Header hashes:
CAS `3b2f0d5b…`, fetch_add `a0c50986…` (recorded in each `metadata.txt`).

## Analysis

With one hot line, the fetch_add increment cut the per-pair cost by 36-38% at two
threads (156.47 to 96.45 ns, repeat 141.88 to 91.11 ns) and 40-44% at four threads
(367.72 to 206.42 ns, repeat 321.05 to 192.89 ns). Before the change own was
within noise of, or slower than, `std::shared_ptr` under contention (367.72 vs
326.83 ns at four threads); after it, own is faster in both runs (206.42 vs
373.39 ns, 192.89 vs 342.53 ns).

The controls behave as the mechanism predicts. Without sharing
(`private_copy_drop`) the change makes no measurable difference, because an
uncontended `lock xadd` and an uncontended `lock cmpxchg` cost about the same.
Weak locking, which still uses compare/exchange in both libraries, did not
change beyond run-to-run noise. The saved cost therefore comes from the retry loop
itself: a failed compare/exchange has to fetch the line again before retrying,
while a locked add always succeeds on the first ownership of the line.

The [codegen record](../benchmarks/results/scaling_increment_codegen.txt) shows the
two instruction sequences: the old copy loops on `lock cmpxchgq`, the new copy is
one `lock xaddq` followed by a compare against the saturation bound.

`std::shared_ptr` remains slower than the new own increment here. One plausible
contributor is that libstdc++'s release path first reads both counts together
before decrementing, an extra access to the contended line; this run does not
isolate that.

## Limits

- One 4-vCPU VM without pinning or frequency control. CV reaches 0.20 for some
  contended cases, and std's own four-thread median moved from 327 to 373 ns
  between identical-code processes, so differences under about 15% in contended
  rows are not established by this data
- No SMT, no multi-socket or NUMA, and no Arm. On Arm with LSE the change maps to
  `ldadd` versus a `cas` loop and is expected to help similarly; without LSE both
  become load-exclusive/store-exclusive loops and the gain may vanish. Unmeasured
- Real programs rarely copy one owner from every core in a tight loop. The result
  shows the worst case for a hot owner (for example a shared asset handed to many
  jobs per frame), not an application speedup
- Clang and MSVC code generation were not measured

## Safety argument for the saturating increment

*Later revision:* the counts now share one 64-bit word with 32-bit halves, and
each half saturates at 2^31 with the same argument applied to 2^31 values of
headroom (see [the count-layout report](benchmark_counts.md)). The text below
describes the measured revision, which used separate 64-bit counters.

Every `increment` caller already holds a reference, so the count is at least one
before the add. A previous value of zero is a use-after-release bug and aborts.
Reaching `saturation_limit` (2^63 - 1 on 64-bit) aborts. Between the threshold and
wrap-around there are another 2^63 values, so wrapping through zero would require
that many increments in flight that have not yet observed the threshold, which no
machine can produce. The abort happens after the add rather than before it; the
process terminates either way. Death tests cover both thresholds for the
increment and for weak locking.

## Reproduce

```sh
scripts/benchmark_scaling.sh                    # full run, about one minute here
QUICK=1 scripts/benchmark_scaling.sh            # smoke test, a few seconds
OWN_INCLUDE=/path/to/other/include scripts/benchmark_scaling.sh   # another header
```

The script refuses to overwrite existing samples. Override `CXX`,
`BENCH_CXXFLAGS`, `BENCH_OUTPUT`, and pass `--samples`, `--warmups`,
`--iterations` or `--max-threads` through to the harness.

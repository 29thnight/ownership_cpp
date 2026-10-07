# Shared ownership and lifetime-bounded borrowing: measured results

## What these measurements support

`shared_owner` carries independent shared lifetime. `local_view` is a pointer-sized,
trivially copyable **non-owning** access handle: an owner must keep the payload alive
for every use. The appropriate standard-library baseline is a retained
`std::shared_ptr` plus a raw pointer borrow with the same lifetime contract.

On this one Linux cloud VM:

- Borrowed by-value parameters measured **1.357 ns/call for both** `local_view` and the standard raw-borrow baseline. Their non-inlined parameter helpers contain [matching instructions](../benchmarks/results/20261007_view_repeat/borrow_parameter_codegen.txt), with no allocation or reference-count operation
- A contiguous 4,096-draw borrow loop measured **2.882 µs own vs 2.927 µs std**; the independent process repeat was **2.918 vs 2.915 µs**. There is no useful measured draw-loop advantage here
- One shared pin at scope entry, original-owner reset, and 1,024 borrowed reads measured **565.887 ns own vs 405.753 ns std**; repeat **566.814 vs 405.144 ns**. Own was slower in this benchmark. The pin is ordinary shared ownership; the loop itself makes no ownership copies
- Shared queue handoff followed by 1,024 worker-local borrowed reads measured **1.351 µs own vs 1.363 µs std/job**; repeat **1.388 vs 1.320 µs**. Queue synchronization and scheduling are included, so this is not isolated borrow or reference-count latency
- The tight single-object borrow loop measured **0.364 ns/read own vs 0.543 ns/read std**. This does not establish a general advantage over raw pointers: surrounding generated code matters, the parameter helpers match, and the pin workload reverses the result

The useful change is making lifetime-bounded borrowing explicit without adding an
ownership allocation, count, or guard. It is **not** replacing independent ownership
with an equivalent cheaper owner. `local_owner` remains an optional,
thread-confined independent-local-lifetime owner with its existing separate group
allocation. This change did not optimize, embed, or merge that group.

No target engine, renderer, graphics API or real GPU workload was measured. A view
is not a borrow checker, cannot detect a dangling lifetime, and provides no payload
synchronization. The optional `enable_owner_from_this` mixin is not exercised by
these timing cases; the payload is an ordinary, non-inheriting asset.

## Reproduce and audit

```sh
BENCH_OUTPUT=benchmarks/results/my_view_run \
scripts/benchmark.sh --samples 101 --warmups 5 --iterations 100000

python3 benchmarks/verify_results.py benchmarks/results/my_view_run
```

The script refuses to overwrite an existing `raw.csv`. Override `CXX`,
`BENCH_CXXFLAGS`, `BENCH_OUTPUT`, or `BENCH_SOURCE` when needed. CSV verification
checks arithmetic, sample counts, matching payload checksums, allocation balance,
and nearest-rank summaries; it is not a statistical significance test or a
sanitizer-success check.

GCC 14.2.0 / libstdc++ was used with:

```text
-std=c++20 -O3 -DNDEBUG -march=native -pthread -Wall -Wextra -Wpedantic
```

No dependencies were installed. `-march=native` is host-specific. `NDEBUG` disables
local-owner thread diagnostics for release timings. `OWN_ENABLE_UNSAFE_GET_WARNING`
was **1**, its default: the benchmark never calls an own raw accessor, and the
standard raw-borrow baseline uses `std::shared_ptr::get()`.

Published metadata is minimized to compiler/flags, CPU model/architecture, OS,
run settings and public source hashes. Workspace paths, host identifiers and raw
system dumps are omitted. Console/diagnostic path and process-ID labels were
redacted; timing CSV values and tested sources are unchanged. File manifests were
regenerated after redaction. The current runner now emits only these minimized
fields; this metadata-only script edit does not change the measured harness.

Current full runs, each with **56 cases × 101 measured samples**, five warmups:

- Primary: [summary](../benchmarks/results/20261007_view_primary/summary.csv), [all samples](../benchmarks/results/20261007_view_primary/raw.csv), [metadata](../benchmarks/results/20261007_view_primary/metadata.txt), [settings/checksum](../benchmarks/results/20261007_view_primary/run.txt), [file hashes](../benchmarks/results/20261007_view_primary/SHA256SUMS)
- Independent process repeat: [summary](../benchmarks/results/20261007_view_repeat/summary.csv), [all samples](../benchmarks/results/20261007_view_repeat/raw.csv), [metadata](../benchmarks/results/20261007_view_repeat/metadata.txt), [settings/checksum](../benchmarks/results/20261007_view_repeat/run.txt), [file hashes](../benchmarks/results/20261007_view_repeat/SHA256SUMS)

Both full runs produced checksum `20708780059296`. Source hashes identify the measured header and harness:

```text
ownership.hpp       896f997b8c5ca0a479ecb21f5c1463510bd7ef64ee4569463c9726ab07cc4e9d
ownership_bench.cpp d62d87e278744f4b16118ae2fbf049f399e8688dc0ead7d83f40d95cc9c70d4a
```

The host reports AMD EPYC 9V74, x86-64, Linux 6.18.44, and GCC/libstdc++ as listed above. No CPU pinning, exclusive reservation, or frequency control was
applied. Virtualization, host scheduling,
frequency changes, code layout, inlining and allocator state limit tiny timing
comparisons. These repeated measurements do not establish worst-case progress,
NUMA behavior, many-core scaling, or a universal crossover point.

## Matched non-owning workloads

All values below are **nanoseconds per stated unit**. Each row's p95/p99 is computed
across **batch-normalized elapsed times**, not individual operation latencies.

| Case | Implementation | Unit | Median | p95 | p99 | Repeat median |
|---|---|---|---:|---:|---:|---:|
| owner_retained_borrow_copy_read | std_raw_borrow | read | 0.543 | 0.691 | 1.131 | 0.545 |
| owner_retained_borrow_copy_read | own_local_view | read | 0.364 | 0.610 | 0.807 | 0.364 |
| owner_retained_borrow_parameter | std_raw_borrow | call | 1.357 | 1.902 | 2.311 | 1.358 |
| owner_retained_borrow_parameter | own_local_view | call | 1.357 | 1.936 | 3.519 | 1.356 |
| resettable_root_pin_1024_borrow_reads | std_shared_pin_raw_borrow | scope | 405.753 | 586.433 | 1276.629 | 405.144 |
| resettable_root_pin_1024_borrow_reads | own_shared_pin_local_view | scope | 565.887 | 1131.052 | 1462.876 | 566.814 |
| owner_retained_4096_draw_borrows | std_raw_borrow | frame | 2927.250 | 5956.333 | 9194.000 | 2915.167 |
| owner_retained_4096_draw_borrows | own_local_view | frame | 2882.208 | 4112.750 | 11311.708 | 2917.625 |
| owner_retained_4096_parameter_borrows | std_raw_borrow | frame | 6702.000 | 8720.000 | 14783.958 | 6691.167 |
| owner_retained_4096_parameter_borrows | own_local_view | frame | 6671.917 | 9092.625 | 17634.000 | 6695.750 |
| async_queue_job_1_borrow_reads | std_shared_then_raw_borrow | job | 497.240 | 1411.890 | 2437.800 | 477.610 |
| async_queue_job_1_borrow_reads | own_shared_then_local_view | job | 461.380 | 894.630 | 1756.590 | 470.490 |
| async_queue_job_64_borrow_reads | std_shared_then_raw_borrow | job | 554.120 | 925.770 | 2316.630 | 636.450 |
| async_queue_job_64_borrow_reads | own_shared_then_local_view | job | 554.420 | 887.320 | 1013.900 | 591.970 |
| async_queue_job_1024_borrow_reads | std_shared_then_raw_borrow | job | 1363.120 | 2353.080 | 2729.240 | 1320.450 |
| async_queue_job_1024_borrow_reads | own_shared_then_local_view | job | 1350.800 | 2680.860 | 3561.870 | 1388.260 |

The cases have explicit lifetime boundaries:

1. `owner_retained_borrow_*`: one existing owner remains alive and unchanged for the whole loop. Both implementations copy only non-owning handles. The parameter case uses the same non-inlined by-value helper and payload read
2. `resettable_root_pin_*`: copy the shared owner **once** at scope entry, reset the sole original owner, perform 1,024 reads through borrowed copies, and move the pin back to prepare the next scope. Timing includes pin copy, original reset and restoration. Untimed destructor/count checks verify that the pin, not a view, retains the payload
3. `owner_retained_4096_*`: a retained array of 64 shared owners backs a contiguous array of 4,096 non-owning draw parameters on both sides. Array creation is outside timing; no owner is reset, erased or hot-reloaded during a draw loop. The parameter variant crosses the same non-inlined helper once per draw
4. `async_queue_job_*_borrow_reads`: shared owners cross an identical synchronized deque into a persistent worker. The receiving job owner remains alive throughout its borrowed scope. Views/raw borrows do not cross the queue. Queue/deque allocation, locks, wakeups, dispatch and completion waiting are included

A view cannot substitute for an asynchronous lease, stored owner, or pin that must
outlive the owner supplying it. Weak ownership is first locked into a named shared
owner, then borrowed while that owner remains alive.

## Independent owning copies are a separate contract

These cases deliberately give each temporary or by-value parameter its own
lifetime responsibility. A borrowed pointer/view does not supply that guarantee.
The root stays alive so final payload destruction is not mixed into copy costs.

| Case | Implementation | Unit | Median | p95 | p99 | Repeat median |
|---|---|---|---:|---:|---:|---:|
| copy_read_drop | std_shared | copy | 11.890 | 14.747 | 15.473 | 12.054 |
| copy_read_drop | own_shared | copy | 4.697 | 5.814 | 6.733 | 4.701 |
| copy_read_drop | own_local | copy | 1.218 | 1.809 | 3.493 | 1.222 |
| independent_owner_parameter | std_shared | call | 11.906 | 14.517 | 16.193 | 11.977 |
| independent_owner_parameter | own_shared | call | 4.482 | 6.445 | 9.697 | 4.506 |
| independent_owner_parameter | own_local | call | 1.628 | 2.643 | 3.393 | 1.629 |

`local_owner` copies can amortize their existing local-group setup when independent
thread-confined ownership is actually needed. Their speed relative to shared
copies is not a reason to create a group or ownership copies in a lifetime-bounded
borrow loop. The original group setup, weak-lock, shared handoff, construction,
contention, scene, lifecycle, and independently owned worker-copy cases are all
still present in the 56-case CSVs.

## Actual prior-source rerun and unchanged-harness control

The [published report at ae6caf8](benchmark_results_ae6caf8.md) is preserved
**byte-for-byte**, as are its [original primary](../benchmarks/results/20261007_primary/)
and [original repeat](../benchmarks/results/20261007_repeat/) artifacts. Its framing
and results are historical, not newly measured view-first results.

A separate Git checkout was detached at the actual published commit
`ae6caf8334c6e1b2777d03291065513b5eadeeb7`. Its real header, benchmark and script were
compiled and run twice with the same GCC 14.2.0 compiler and release flags used
above. This is not an emulation of the old API on the current implementation:

- Old-source rerun primary: [summary](../benchmarks/results/20261007_ae6caf8_rerun_primary/summary.csv), [raw](../benchmarks/results/20261007_ae6caf8_rerun_primary/raw.csv), [metadata](../benchmarks/results/20261007_ae6caf8_rerun_primary/metadata.txt)
- Old-source rerun repeat: [summary](../benchmarks/results/20261007_ae6caf8_rerun_repeat/summary.csv), [raw](../benchmarks/results/20261007_ae6caf8_rerun_repeat/raw.csv), [metadata](../benchmarks/results/20261007_ae6caf8_rerun_repeat/metadata.txt)

The byte-exact [old benchmark source](../benchmarks/baseline_ae6caf8/ownership_bench.cpp)
was then compiled against the current frozen header, without accessor edits or any
other source compatibility transformation:

- Current-header control primary: [summary](../benchmarks/results/20261007_current_header_control_primary/summary.csv), [raw](../benchmarks/results/20261007_current_header_control_primary/raw.csv), [metadata](../benchmarks/results/20261007_current_header_control_primary/metadata.txt)
- Current-header control repeat: [summary](../benchmarks/results/20261007_current_header_control_repeat/summary.csv), [raw](../benchmarks/results/20261007_current_header_control_repeat/raw.csv), [metadata](../benchmarks/results/20261007_current_header_control_repeat/metadata.txt)

All four controls retain the exact original **37 cases**, 101 samples and five
warmups, with checksum `20688493065280`. Old header SHA-256:
`f2a8a71b1bdf30311618a92aa657d01b39459290df6a88d0a49c1b9c6f3f6c88`.
Unchanged harness SHA-256:
`cafb96ef1d60afc4fe251057a3fc053772d31fbd3105e5f1a453e5bf0884feae`.

Selected **median ns/unit** below use the fresh reruns, not the original published
numbers. [All 37 temporal comparisons](../benchmarks/results/comparison_ae6caf8_to_view.csv)
are retained, including the published-primary column.

| Case | Implementation | Unit | Old primary | Old repeat | Current primary | Current repeat |
|---|---|---|---:|---:|---:|---:|
| copy_read_drop | own_shared | copy | 4.792 | 4.700 | 5.033 | 4.677 |
| copy_read_drop | own_local | copy | 1.221 | 1.221 | 1.225 | 1.219 |
| group_setup_then_1_copies | own_local | group | 20.177 | 25.757 | 21.064 | 19.611 |
| create_read_destroy | std_shared | asset | 12.024 | 11.838 | 12.961 | 11.828 |
| create_read_destroy | own_shared | asset | 13.593 | 12.639 | 14.341 | 13.119 |
| create_read_destroy | own_local | asset | 23.353 | 22.440 | 23.960 | 22.530 |
| frame_4096_draws_64_unique_leases | std_shared | frame | 3897.458 | 3879.917 | 3852.750 | 3840.667 |
| frame_4096_draws_64_unique_leases | own_shared | frame | 4008.417 | 3878.208 | 3998.000 | 3901.208 |
| resource_lifecycle_64_assets_4096_objects | std_shared | cycle | 37573.500 | 34895.600 | 39622.600 | 36133.400 |
| resource_lifecycle_64_assets_4096_objects | own_local | cycle | 25021.000 | 24300.000 | 27251.300 | 24934.000 |

These controls do not establish an ownership-algorithm or allocation improvement;
the existing plain-payload allocation/count design is unchanged. Both libraries'
measurements vary between processes. In particular, the expanded harness's own
`group_setup_then_1_copies` median is 11.219 ns, while the exact old-harness/current-
header control is 21.064 ns **using the same header**. Additional code changes the
optimization/layout context and case ordering; no causal library speedup is
claimed from that discrepancy. Use the unchanged-harness control for historical
comparisons and the matched new cases for borrowing comparisons.

To repeat the current-header control:

```sh
BENCH_SOURCE=benchmarks/baseline_ae6caf8/ownership_bench.cpp \
BENCH_OUTPUT=benchmarks/results/my_current_header_control \
scripts/benchmark.sh --samples 101 --warmups 5 --iterations 100000
```

## Allocation and space overhead

[Allocation counts](../benchmarks/results/20261007_view_primary/allocations.csv) use
equivalent custom allocation callbacks, outside timing. The standard allocator is
stateless, avoiding an instrumentation pointer in its control block. Counts and
bytes concern ownership storage; common vector/deque backing storage is excluded.
Bytes are requested sizes, not allocator size classes, RSS or peak resident memory.
Every tracked allocation was freed.

| Scenario | std allocations / requested bytes | own allocations / requested bytes |
|---|---:|---:|
| Create one shared asset | 1 / 104 | 1 / 160 |
| Retain one shared asset and form 4,096 borrows | 1 / 104 | 1 / 160 |
| Extra ownership allocations caused by those borrows | 0 / 0 | 0 / 0 |
| Create one optional local owner | 1 / 104 | 2 / 200 |
| One optional reusable local group, 4,096 owning attachments | 1 / 104 | 2 / 200 |
| 4,096 independent localizations | 1 / 104 | 4,097 / 164,000 |

The borrow checks also verify that forming/copying all 4,096 handles does not change
the retained owner's strong count. This ordinary `owner.borrow()` path has no
control-block access or guard. The owner allocation exists before borrowing and
is not attributable to a view.

[Observed ABI sizes](../benchmarks/results/20261007_view_primary/sizes.csv): payload
88 bytes; `local_view` and raw pointer **8 bytes**; own shared/local/weak handles and
standard shared/weak handles **16 bytes**. A compile-time assertion checks that the
view is pointer-sized and trivially copyable. Existing own shared control overhead
is 72 bytes, with a **separate 40-byte group allocation** for each optional local
owner group. No group was embedded or coallocated by this change. ABI sizes are
implementation-specific, not promises for other platforms.

## Measurement controls

- Five warmup rounds, then 101 measured rounds; the order of all cases is shuffled each round with seed `0xC0FFEE`. No samples are trimmed
- A thread is created and joined before timing. The recorded glibc flag is `__libc_single_threaded == 0`, keeping libstdc++ on its multithreaded reference-count path
- Matched implementations use the same deterministic asset reads and matching per-case checksums. Temporary handles escape through a compiler memory barrier to resist elision. Parameter cases add the same explicit non-inlined boundary. No LTO is used
- Factory cases separately include creation and final destruction. The root remains alive in copy/parameter tests. The original lifecycle case includes registry construction, optional local-group setup, 4,096 owning scene attachments, hot reload, expired weak cache and unload
- The historical frame comparison deduplicates to one global lease per resource and borrows existing scene owners on both sides. It does not charge std an unnecessary owning copy per draw
- Two persistent contending workers copy one shared control block. Their reported per-copy values divide aggregate elapsed time by **both workers' combined operations** and include barrier dispatch/completion; these are throughput-normalized batch costs, not per-thread latency
- Median/p95/p99 use nearest-rank quantiles over batch-normalized elapsed samples. They are not individual copy, job, frame, GPU, or real-time worst-case latency distributions. With 101 samples, tail resolution is limited

## Lifetime and sanitizer validation

The [untimed validation trace](../benchmarks/results/20261007_view_primary/lifetime_validation.txt)
retains the old hot-reload/frame-lease/weak-expiration/deferred-destruction sequence
and adds matched own/std scope-pin checks:

- Publishing asset version 2 does not invalidate independently owned version 1 scene attachments
- A global frame lease retains version 1 after scene release; a worker releases the final lease into a fixed-capacity, mutex-protected retirement queue
- Weak locks fail after the last strong owner disappears, while actual destruction waits until the simulated fence is advanced
- One ordinary shared pin retains the payload after the sole original owner resets; copied views do not change its strong count; a weak owner is locked before borrowing; the last pin destroys the payload exactly once

The retirement queue has 256 slots and terminates on overflow. Its mutex and
callbacks are not bounded real-time operations. Queue/allocator contexts must
outlive their tasks and weak owners, and shutdown must drain outstanding work.
Deferred std validation uses a custom deleter with separate payload/control
storage because `std::make_shared` cannot take that deleter. This validation is
untimed; normal timed std factories use `std::make_shared`.

The final header and 56-case harness passed **ASan+UBSan**, with debug local-owner
thread checks enabled, three samples, one warmup, 1,000 iterations:
[status](../benchmarks/results/20261007_view_sanitizer/status.txt),
[complete log](../benchmarks/results/20261007_view_sanitizer/driver.log),
[metadata/source hashes](../benchmarks/results/20261007_view_sanitizer/metadata.txt).
The benchmark also compiles with `-Werror` and default raw-access warnings enabled.

Leak detection was rechecked with `ASAN_OPTIONS=detect_leaks=1`. That run failed at
process exit because LeakSanitizer reports that it does not work under ptrace:
[matching environment diagnostic from the test run](../tests/results/asan-default.log),
[exit status](../benchmarks/results/20261007_view_sanitizer_leaks_enabled/status.txt).
The leak-enabled benchmark driver log is not part of the published artifacts;
its exit status and measured output remain available. The passing ASan+UBSan run
uses `detect_leaks=0`. **LeakSanitizer leak detection
remains unverified**; explicit allocation/destructor checks do not replace it.
Sanitizer timings are not performance evidence.

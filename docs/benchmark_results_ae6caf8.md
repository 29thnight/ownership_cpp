# Measured ownership and asset-lease costs

## What the measurements support

On this **one Linux cloud VM**, reusing a thread-confined local group made the deliberately ownership-heavy copy loops cheaper. That did **not** turn into a general frame-time improvement:

- Copy/read/drop: `own::local_owner` **1.220 ns**, `std::shared_ptr` **11.897 ns** median. A raw borrow was **0.543 ns**; do not introduce ownership copies when a borrow is sufficient
- Including a fresh group and only one local copy: **19.275 ns own vs 19.248 ns std**, effectively equal here. Reuse is essential; this is not a universal crossover threshold
- Construction/destruction: **12.708 ns own shared**, **22.219 ns own local**, **11.657 ns std**. Own local creation paid for an extra allocation
- Optimized deduplicated frame (4,096 draws, 64 global leases): **3.942 µs own vs 3.823 µs std**. The repeat was **3.844 vs 3.789 µs**. There is no measured frame win; small differences are within the limitations of this host
- Full synthetic asset lifecycle, including 64 old/new versions, 4,096 scene attachments, hot reload, expired weak cache and unload: **23.949 µs own vs 35.143 µs std**. This is a CPU simulation, not an engine integration
- Queue jobs with only one local copy were about equal. At 1,024 ownership copies/job: **4.136 µs own vs 17.342 µs std**, including enqueue/wakeup/completion and group setup

Global `own::shared_owner` copies also measured faster than this libstdc++ implementation, but that is **not** an intrinsic guarantee of atomic reference counting or a complete `std::shared_ptr` feature comparison. Own's overflow-checked CAS increment can retry under contention. Two workers are measured; many-core scaling, NUMA traffic and worst-case progress are not established.

## Reproduce

From the repository root:

```sh
BENCH_OUTPUT=benchmarks/results/my_run scripts/benchmark.sh --samples 101 --warmups 5 --iterations 100000
```

Requires a C++20 compiler with standard threads/barriers. The script used GCC 14.2.0 and:

```text
-std=c++20 -O3 -DNDEBUG -march=native -pthread -Wall -Wextra -Wpedantic
```

Override `CXX`, `BENCH_CXXFLAGS` or `BENCH_OUTPUT` if needed. `-march=native` makes the produced binary host-specific. Do not use sanitizer runs for performance conclusions. `NDEBUG` disables the library's local-thread diagnostics, as expected for its release build.

Primary data: [summary](../benchmarks/results/20261007_primary/summary.csv), [every timed sample](../benchmarks/results/20261007_primary/raw.csv), [machine/compiler/source metadata](../benchmarks/results/20261007_primary/metadata.txt), [run settings](../benchmarks/results/20261007_primary/run.txt)

Independent process repeat: [summary](../benchmarks/results/20261007_repeat/summary.csv), [every timed sample](../benchmarks/results/20261007_repeat/raw.csv), [metadata](../benchmarks/results/20261007_repeat/metadata.txt)

Both runs use the same header and benchmark SHA-256 hashes:

```text
ownership.hpp       f2a8a71b1bdf30311618a92aa657d01b39459290df6a88d0a49c1b9c6f3f6c88
ownership_bench.cpp cafb96ef1d60afc4fe251057a3fc053772d31fbd3105e5f1a453e5bf0884feae
```

The recorded Git HEAD predates this uncommitted implementation; the file hashes identify the measured sources. CPU reports AMD EPYC 9V74, x86-64, nine available virtual CPUs (affinity 0–8), Linux 6.18.44. No CPU pinning or exclusive CPU reservation was applied. CPU quota/governor were unavailable in the container. Host scheduler, virtualization and frequency noise remain uncontrolled.

## Measurement controls and interpretation

- 101 measured rounds after five warmup rounds; case order is deterministically shuffled each round (seed `0xC0FFEE`). A second independent process repeats the whole run
- A thread is created and joined **before timing**. On glibc, the harness also checks and records `__libc_single_threaded == 0`, preventing the standard library's single-thread reference-count shortcut from flattering the baseline
- All paired cases execute the same deterministic 88-byte asset payload work. Escaping each temporary through a compiler memory barrier prevents optimizing ownership copies away; the generated binary contains locked atomics. No LTO is used
- Each case's checksum is checked against the other implementations and all later samples. Both final timing runs produced total checksum `20688493065280`
- The root remains alive in copy/read/drop tests; factory tests separately include allocation and final destruction. Shared handoff, weak live/expired locks, group setup plus 1/8/64/1,024 copies, and contended global shared copies are included
- Scene attachments reuse **one local group per resource**. Timed scene reattachment excludes initial registry/group construction, while the full lifecycle case includes it
- Both frame implementations deduplicate to one global lease per resource, and borrow existing scene owners during draw submission. Neither is charged an artificial ownership copy per draw
- Async jobs use an identical synchronized deque and persistent worker. Only global shared owners cross the thread boundary. Each worker localizes once, uses/destroys all local aliases there, then releases its job owner. Results include queue/deque allocation, locks, wakeups, dispatch and waiting; they are not pure reference-count costs
- Contention uses two persistent workers copying the same control block. Values are aggregate elapsed time divided by **both workers' total copies**; they are throughput-normalized costs, not per-thread latencies. Barrier dispatch/completion is included in both
- Median, p95 and p99 use the nearest-rank distribution of **batch-normalized elapsed times**. They are not distributions of individual copy latency, individual job latency, real-time worst cases, or GPU frame latency. No outlier trimming is applied; 101 rounds give limited tail resolution

## Selected results

All values below are **nanoseconds per stated unit**. The primary p95/p99 and repeat median are shown; every case, minimum, maximum and raw sample is retained in CSV.

| Case | Implementation | Unit | Median | p95 | p99 | Repeat median |
|---|---|---|---:|---:|---:|---:|
| copy_read_drop | std_shared | copy | 11.897 | 14.599 | 20.287 | 11.735 |
| copy_read_drop | own_shared | copy | 4.722 | 6.762 | 10.434 | 4.685 |
| copy_read_drop | own_local | copy | 1.220 | 1.723 | 3.179 | 1.220 |
| borrow_read | raw_borrow | read | 0.543 | 0.908 | 1.135 | 0.544 |
| localize_read_drop | std_shared | group | 12.073 | 15.511 | 22.590 | 11.946 |
| localize_read_drop | own_local | group | 11.113 | 14.009 | 24.980 | 11.047 |
| group_setup_then_1_copies | std_shared | group | 19.248 | 23.005 | 33.482 | 18.917 |
| group_setup_then_1_copies | own_local | group | 19.275 | 27.269 | 37.641 | 18.996 |
| group_setup_then_64_copies | std_shared | group | 763.844 | 958.935 | 1073.265 | 758.478 |
| group_setup_then_64_copies | own_local | group | 110.817 | 161.250 | 222.974 | 106.976 |
| create_read_destroy | std_shared | asset | 11.657 | 15.532 | 21.653 | 11.368 |
| create_read_destroy | own_shared | asset | 12.708 | 15.378 | 20.976 | 12.456 |
| create_read_destroy | own_local | asset | 22.219 | 28.649 | 43.734 | 21.773 |
| contended_copy_2_workers | std_shared | copy | 20.513 | 25.329 | 25.799 | 23.113 |
| contended_copy_2_workers | own_shared | copy | 12.185 | 23.665 | 23.906 | 12.570 |
| scene_4096_attachments | std_shared | attachment | 6.756 | 8.848 | 9.301 | 5.152 |
| scene_4096_attachments | own_local | attachment | 1.496 | 2.442 | 2.791 | 1.500 |
| frame_4096_draws_64_unique_leases | std_shared | frame | 3823.167 | 5873.250 | 9001.625 | 3789.375 |
| frame_4096_draws_64_unique_leases | own_shared | frame | 3941.667 | 7407.625 | 12023.208 | 3844.000 |
| resource_lifecycle_64_assets_4096_objects | std_shared | cycle | 35143.000 | 50783.100 | 71608.900 | 34249.600 |
| resource_lifecycle_64_assets_4096_objects | own_local | cycle | 23948.500 | 33489.500 | 68399.200 | 24114.600 |
| async_queue_job_1_local_copies | std_shared | job | 550.910 | 960.820 | 1241.230 | 513.260 |
| async_queue_job_1_local_copies | own_local | job | 560.020 | 1006.490 | 1778.330 | 511.160 |
| async_queue_job_1024_local_copies | std_shared | job | 17342.240 | 20382.940 | 26975.810 | 16476.970 |
| async_queue_job_1024_local_copies | own_local | job | 4135.520 | 5880.300 | 6676.970 | 3983.990 |

## Allocation and space overhead

[Allocation counts](../benchmarks/results/20261007_primary/allocations.csv) are measured through equivalent custom allocation callbacks, separately from timings. The standard counting allocator is stateless so instrumentation does not add an allocator pointer to the standard control block. These are **requested bytes**, not allocator size classes, RSS or peak resident memory. Common vector/deque storage is excluded from this ownership-allocation report. All tracked allocations were matched by frees.

| Scenario | std allocations / bytes | own allocations / bytes |
|---|---:|---:|
| Create one shared asset | 1 / 104 | 1 / 160 |
| Create one local asset | 1 / 104 | 2 / 200 |
| One reusable group, 4,096 attachments | 1 / 104 | 2 / 200 |
| 4,096 independent localizations | 1 / 104 | 4,097 / 164,000 |

The payload is 88 bytes. Own's observed coallocated control overhead is 72 bytes plus **40 bytes per local group** in this release build. Handles are 16 bytes each for own local/shared/weak and standard shared/weak on this ABI. Sizes are implementation-specific, not API promises. Repeatedly calling `localize()` for each object recreates groups; local-copy an existing group instead. Weak owners retain the coallocated backing allocation after payload destruction, as with `make_shared`.

## Engine-like lifetime validation and boundaries

The [untimed validation trace](../benchmarks/results/20261007_primary/lifetime_validation.txt) verifies both implementations:

1. Registry publishes version 2 while existing scene attachments still reference version 1
2. A global frame lease keeps version 1 alive after scene release
3. A worker thread drops the final shared frame lease and submits destruction to a mutex-protected, allocation-free fixed-capacity retirement queue
4. Weak caches immediately expire and cannot relock, while payload destruction remains delayed
5. Advancing a simulated completion fence from 2 to 3 destroys version 1 exactly once; registry/cache release and fence 6 retire version 2 exactly once

The queue has 256 slots and terminates on overflow. It is a demonstration of the `noexcept` retirement contract, not a production queue/backpressure strategy. Its mutex acquisition and destructor callbacks are not bounded real-time operations. Queue and allocator contexts must outlive all retained tasks/weak owners. Shutdown must eventually drain retirement tasks.

The std deferred baseline uses a custom deleter with separate payload/control allocations, since `std::make_shared` cannot accept a custom deleter. **Deferred-destruction validation is untimed**, so this representation difference is not hidden inside the reported ownership timings. The normal timed std factory is `std::make_shared`, and own factories coallocate payload/control.

No renderer, asset I/O, ECS, actual GPU, graphics API fence, real resource reclamation, cache working set sweep or target engine was integrated. A correct smart pointer does not solve GPU completion or make unsynchronized payload access safe. Local aliases may not be copied, moved, used or destroyed on another thread; share first, then localize on the receiving worker.

## Verification

The final benchmark source also passed an ASan+UBSan smoke (3 rounds, 1 warmup, 1,000 iterations) with debug thread checks enabled. See [its metadata](../benchmarks/results/sanitizer_validation/metadata.txt) and [status](../benchmarks/results/sanitizer_validation/status.txt). `ASAN_OPTIONS=detect_leaks=0` was necessary: an earlier LeakSanitizer run failed because the execution environment uses ptrace. **LeakSanitizer leak detection is unverified**; the explicit allocation and destructor checks are not a substitute for it. Sanitizer timings are not performance evidence.

# Re-measurement and optimization of the slow cases

All benchmarks were re-run on the head before this round (`daab26a`), the cases
where own was consistently slower than its contract-matched standard owner were
optimized one at a time, and everything was re-run on the final head.

## Method (seven fields)

1. **CPU:** Intel Xeon Processor @ 2.30GHz (cloud VM, x86-64), Linux 6.18.44
2. **Cores used:** main harness unpinned (as in its earlier reports); scaling and
   layout harnesses pinned with `--pin 1`; allocated-unique pinned with `taskset`
3. **Frequency:** 2300 MHz nominal; governor and turbo not visible from the guest.
   SMT: 1 thread per core as reported
4. **Compiler:** GCC 13.3.0. Main harness `-std=c++20 -O3 -DNDEBUG -march=native`;
   the others `-std=c++20 -O2 -DNDEBUG`
5. **Workload:** the existing harnesses, unchanged
6. **Baseline:** `std::shared_ptr`/`std::unique_ptr` in the same binary
7. **Method:** main harness 101 samples after 5 warmups, two processes per head,
   CSV verified with `benchmarks/verify_results.py` (checksum 20708780059296 in all
   four runs); others as in their reports

Data: before [primary](../benchmarks/results/remeasure_main_primary/summary.csv) /
[repeat](../benchmarks/results/remeasure_main_repeat/summary.csv), after
[primary](../benchmarks/results/final_main_primary/summary.csv) /
[repeat](../benchmarks/results/final_main_repeat/summary.csv); `remeasure_*` and
`final_*` directories for the other harnesses; `optimized_*` is an intermediate
head (`39bf43f`).

## Slow cases found and what changed

| Case (main harness) | Before own/std | After own/std | own ns before → after | Change |
|---|---:|---:|---:|---|
| `make_local` create/read/destroy | 1.94 / 1.94 | **1.11 / 1.10** | 23.5 → 13.4 | one allocation, then exclusive release |
| `localize` read/drop | 1.21 / 1.21 | **0.92 / 0.93** | 18.1 → 13.8 | per-thread group cache |
| `make_shared` create/read/destroy | 1.48 / 1.49 | 1.48 / 1.46 | 17.9 → 18.0 | not changed, see below |
| group setup + 1 copy | 0.76 / 0.76 | 0.42 / 0.42 | 25.4 → 14.3 | group cache |
| group setup + 64 copies | 0.15 / 0.17 | 0.08 / 0.08 | 158.7 → 89.2 | group cache |
| async queue, 1,024 local copies | 0.15 / 0.14 | 0.11 / 0.10 | 3639 → 2588 | one allocation, cache |
| resource lifecycle, 64 assets | 0.45 / 0.45 | 0.39 / 0.39 | 23518 → 20318 | one allocation |

| Other harnesses | Before | After | Change |
|---|---:|---:|---|
| `allocated_unique_owner` move (vs std erased 1.79 ns) | 2.11 ns | **1.80 ns** | direct move assignment |
| `allocate_unique` create (vs std erased 9.95 ns) | 11.06 ns | **10.04 ns** | default allocator inlined |
| `make_shared` create, layout harness | 18.1 ns | 16.7 ns | default allocator inlined |

## The changes

1. **One allocation for local factories.** `make_local` and `allocate_local(_with)`
   reserve their first group inside the control block. The group holds a strong
   reference, so its storage never outlives the block; releasing it only ends the
   group object. With no second allocation, a factory cannot fail after payload
   construction, and the retirement hook is installed at construction.
2. **Per-thread group cache.** Default-allocator `localize()` reuses one freed
   group's storage per thread. Groups are released on their creating thread, so
   the slot is unsynchronized; a `thread_local` cleanup frees it at thread exit.
3. **Exclusive local groups.** A factory-created group whose strong reference is
   the only reference of any kind (no `share()`, weak observer or registration)
   retires the strong count with a plain store instead of an atomic decrement.
   The flag is the low bit of the alias count; `share()` and weak observation
   clear it, and only when it is set.
4. **Inlined default allocation.** `allocate_bytes` calls `default_allocate`
   directly for the default allocator, so it inlines into `::operator new` and
   the null-result check disappears.
5. **Direct move assignment** for `allocated_unique_owner`, with the same order
   as before (new object installed before the old one is disposed).

## Regressions found during the round and fixed

The first exclusive-group commit regressed two cases, caught by re-measuring
before publishing: `share_read_drop` 11.3 → 13.2 ns (an unconditional store just
before a locked increment) and `scene_4096_attachments` 1.46 → 1.99 ns (a masking
operation plus duplicated teardown code inlined into alias loops). Moving the
flag to the low bit, writing it only when set and moving the last-alias path out
of line restored both (11.3 ns and 1.31/1.39 ns in the final runs).

`contended_copy_2_workers` read 41/46 ns before and 61/58 ns after, but the
generated code for shared copies is byte-for-byte identical between the two heads
and the pinned scaling harness shows no change (two threads 113 → 104 ns, four
threads 263 → 249 ns). With a coefficient of variation of 0.2-0.3 in this unpinned
case, the difference is attributed to run-to-run noise.

## Follow-up: both fast creation and fast copies

The trade-off above came from checking the counts on every release. A follow-up
(`6a6455f`) checks only on the handle a factory returned: a hint in the low bit
of its block address, carried by moves and cleared by copies, so copies keep a
single `fetch_sub` release. Main harness, 101 samples x 2, against the head
above ([primary](../benchmarks/results/hint_main_primary/summary.csv),
[repeat](../benchmarks/results/hint_main_repeat/summary.csv)):

| Case | Before | After | own/std after |
|---|---:|---:|---:|
| `make_shared` create/read/destroy | 17.99 / 17.88 | **12.59 / 12.64** | **1.00 / 1.00** |
| copy_read_drop (shared) | 13.19 / 13.17 | **11.28 / 11.26** | 0.67 / 0.67 |
| group setup + 64 copies | 89.16 / 89.54 | 74.38 / 77.38 | 0.07 / 0.07 |
| `make_local` create/read/destroy | 13.44 / 13.49 | 15.31 / 15.35 | 1.21 / 1.22 |
| independent_owner_parameter (shared) | 13.16 / 13.12 | 14.74 / 14.69 | 0.88 / 0.88 |
| scene_4096_attachments | 1.31 / 1.39 | 1.51 / 1.46 | 0.16 / 0.15 |

The pinned scaling harness confirms copies did not slow under contention (four
threads 249 -> 210 ns, one thread 13.6 -> 12.4 ns), and the layout harness has
own's creation at 11.27 ns against std's 11.78.

Three main-harness rows moved the wrong way. `make_local`'s generated code is
byte-for-byte identical before and after (it never touches the hinted handle),
and its change shrinks to +4% when both binaries are built with 64-byte function
and loop alignment, so it follows code placement in this harness. The by-value
parameter loop does differ (the copy masks the hint, the release tests it), but
the same loop isolated in a pinned microbenchmark is 9% faster (16.2 -> 14.7 ns);
in this harness it stays 12% slower and still beats std. `scene_4096_attachments`
has ranged from 1.27 to 1.59 ns across earlier runs of unchanged code.

## What stays slower, and why

- **`make_shared` create/read/destroy:** resolved by the follow-up above (1.00x).
- **`make_local` create/read/destroy, 1.10-1.22x** (13.4-15.3 vs 12.2 ns): the
  group initialization and its out-of-line release, plus code placement in the
  main harness (see the follow-up).
- **`async_queue_job_*_borrow_reads`, up to 1.10x:** own's times did not change
  (695/675 ns vs 719/679 ns before); the standard side measured faster in this
  run. These cases include a mutex and condition variable.
- **`owner_retained_borrow_copy_read`, 1.06x** (0.32 vs 0.30 ns): unchanged from
  before this round and from the earlier report, which attributes it to code
  layout around an identical borrowed pointer.
- **Reader beside copier threads** (layout harness): the line-sharing tradeoff
  of the compact header, with `alignas(64)` as the documented opt-out.

(for example for a deliberately audited C API boundary). The default is `1`;
`-Werror=deprecated-declarations` turns this warning into an error. Use one value
consistently across every translation unit. Ordinary `*`, `->`, `bool` and `borrow()`
do not trigger it.

## Optional independent thread-local ownership

Use `local_owner<T>` only when each local copy needs its own independent lifetime,
and all operations, including move and destruction, stay on its group's originating
thread. A group owns one global strong reference; its owning aliases use a
non-atomic local count. `share()` creates a global owner; `shared.localize()` creates
a fresh local group. Cancellation and migrating callbacks must not carry local
owners across threads. Share across the boundary, then localize if needed.

Thread misuse aborts in builds without `NDEBUG` (override with
`OWN_DEBUG_THREAD_CHECK=0/1`). Translation units may differ in this setting
without memory-safety consequences; diagnostics are guaranteed only when every
translation unit enables them.

`local` does not mean an automatically enforced lexical lifetime. `local_owner`
has actual thread confinement; `local_view` merely borrows a pointer and contains
no thread ID, owner, or automatic validity check. Borrowed payload access still
needs appropriate lifetime and synchronization.

`make_shared` coallocates object and a compact control block once (a 16-byte
header, as dense as the standard library's); `make_local` and `allocate_local`
place their first 40-byte local group in the same block, also one allocation.
Every further nonempty `localize()` creates a separate group; with the default
allocator each thread reuses the storage of its most recently freed group, so
repeated localize/drop cycles do not reach the allocator.

Destroying an object that was never shared avoids an atomic read-modify-write
where it can. The handle a factory returns carries a hint (copies clear it) and
checks the counts once on release, skipping the decrement when it is the sole
reference; copies release with one `fetch_sub`. A local group created by
`make_local` that was never shared or observed releases with a plain store. A
payload read on one core while other cores copy its owners shares a cache line
with the counts; declare such a hot type `alignas(64)` to give it its own line
([measurements](docs/benchmark_layout.md)).

## Allocation and deferred retirement

Factories: `make_unique`, `allocate_unique`, `make_shared`, `make_local`,
`allocate_shared`, `allocate_local`. Default `make_unique` uses typed new/delete;
`allocate_unique` returns the separate allocator-aware owner.
An `allocator_ref` supplies non-owning byte-allocation callbacks with exact
size/alignment matching. Null allocations throw `std::bad_alloc`; over-aligned
payloads and constructor failure are supported. Keep allocator contexts alive
until all corresponding control blocks, weak owners and deferred tasks are gone.

The shared/local `_with` factory variants accept a `retirement_hook`. Last strong release makes
weak observers expire immediately, then a move-only `retirement_task` can postpone
payload destruction. The engine must associate and wait for the actual GPU fence;
the library does not do GPU synchronization. Queues must define shutdown and
capacity behavior. See [allocation and retirement contracts](docs/design.md).

## Build, test and reproduce

```sh
./scripts/test.sh
./scripts/benchmark.sh
make example
# This target runs the current debug/release tests before comparing unique owners:
make benchmark-unique
# Reference-count contention at 1..N threads (QUICK=1 for a smoke test):
make benchmark-scaling
# Control-block footprint, scans and cache-line sharing (QUICK=1 for a smoke test):
make benchmark-layout
# Allocator-aware exclusive owner layout (measurement only):
make benchmark-allocated-unique
```

The scaling and layout harnesses accept `--pin 1` to give each thread its own CPU,
include a second identical std implementation as an A/A noise control, and record
the CPU, cores, frequency, SMT, compiler flags, workload, baseline and method for
every run.

Requires an existing C++20 compiler with exceptions and a POSIX shell for the
supplied runners. The library itself is a single header under `include/own`.

The current [unique validation record](tests/results/unique/README.md) includes
unique cases and reruns of the full regression suite. The earlier
[view/from-this record](tests/results/README.md) remains historical evidence.
Validation records contain exact coverage, commands,
source hashes and sanitizer limits. [Benchmark results](docs/benchmark_results.md)
compare equivalent retained-owner borrowing against std ownership plus raw/reference
borrows, and separately measure independent owning copies. Historical data and
fresh reruns of the previous revision are preserved. A borrowed view is not a
faster substitute for an independent lifetime guarantee.
[Contention scaling](docs/benchmark_scaling.md) measures shared-owner copies
from 1 to N threads against `std::shared_ptr`, with private-object and weak-lock
controls; the single-instruction increment cut four-thread copy/drop cost by
about 40% on the measured VM. The [performance review](docs/performance_review.md)
lists the remaining measured costs and planned improvements, and the
[optimization round](docs/optimization_round.md) records the latest before/after
comparison across every harness.

GCC 13 and Clang 18 on Linux x86-64 pass every mode (debug, release, ASan, UBSan,
TSan) locally and in CI (`.github/workflows/ci.yml`), which also compiles every
benchmark harness with `-Werror`, smoke-runs them and runs the examples. Clang's
TSan builds link the shared sanitizer runtime because the allocation-failure tests
replace the global `operator new`. Windows/MSVC, macOS and other architectures are
not yet validated; Arm code generation was inspected but not timed. These are CPU
simulations, not measured engine integration or actual GPU execution.

## Current measured costs

GCC 13.3, `-O3 -DNDEBUG -march=native`, one 4-vCPU Intel Xeon cloud VM, main
harness with 101 samples after 5 warmups in two independent processes
(primary / repeat, median ns per operation). Each case uses the same lifetime
contract on both sides:

| Workload | own | `std::shared_ptr` |
| --- | ---: | ---: |
| `make_shared`, read, destroy (never shared) | 12.59 / 12.64 | 12.64 / 12.61 |
| `make_local`, read, destroy | 15.31 / 15.35 | 12.64 / 12.61 |
| Shared copy, read, drop | 11.28 / 11.26 | 16.82 / 16.83 |
| Local alias copy, read, drop | 1.02 / 1.02 | 16.82 / 16.83 |
| Weak lock of a live object | 15.05 / 15.09 | 17.01 / 17.10 |
| Localize from a shared owner, read, drop | 13.03 / 12.99 | 15.03 / 15.06 |
| Shared copies on two contending workers | 55.54 / 49.88 | 67.96 / 71.02 |
| Shared copy/drop, four threads on one object (pinned) | 209.53 | 347.42 |

`make_local` is the remaining slower row: its generated code is unchanged from
a measurement at 13.4 ns, and the difference follows code placement in this
harness ([analysis](docs/optimization_round.md)). Never-shared objects that need
no shared lifetime are cheapest as `unique_owner`. Data and the method behind
each row are in the [optimization round](docs/optimization_round.md),
[count layout](docs/benchmark_counts.md), [control-block layout](docs/benchmark_layout.md)
and [contention scaling](docs/benchmark_scaling.md) reports.

## Measured borrowing costs (view/from-this revision)

The following published results describe the earlier view/from-this revision and
remain as its record; current costs are above, and exclusive ownership is
measured separately in [unique results](docs/unique_results.md).


GCC 14.2, one Linux x86-64 cloud VM, optimized builds, 101 warmed samples and
an independent process repeat. Both sides retain an owner for the same scope:

| Workload | own view / owner | std owner + raw borrow |
| --- | ---: | ---: |
| Borrowed by-value parameter | 1.357 ns | 1.357 ns |
| 4,096 borrowed draw parameters | 2.882 µs | 2.927 µs |
| One scope pin plus 1,024 reads | 565.887 ns | 405.753 ns |

The repeated draw result was 2.918 vs 2.915 µs: no useful measured draw-loop
advantage. The scope-pin case was slower for own. The benefit is explicit borrowed
access without per-view ownership costs, not a universally faster raw pointer.

The [full report](docs/benchmark_results.md) separates equivalent borrowed
contracts from independent ownership, includes unfavorable results and
batch-normalized p95/p99, and preserves both fresh old-version reruns and the
historical report. Expanding the harness changed some generated code, so old/new
ownership comparisons also use a byte-exact historical-harness control.

## Deliberate boundaries

This is not a replacement for every standard smart-pointer feature. Arbitrary
payload deleters, owning alias constructors and allocator traits are deferred
until an actual integration requires them. Use a resource wrapper with a
`noexcept` destructor for cleanup; `allocator_ref` controls storage separately.
If a dependency needs the full standard pointer interface, using that interface
there can be simpler than expanding this library preemptively.

There is no full `std::unique_ptr` / `std::shared_ptr` parity. Arrays, `void` owners, aliasing ownership,
atomic handle objects, owner-to-owner equality, ordering and hashing, raw adoption,
arbitrary payload deleters and an STL allocator-traits adapter are omitted. Owners
and views compare only with `nullptr` (`owner == nullptr`, `view != nullptr`);
weak observers have no null comparison, use `expired()` or `lock()`. Payload destructors must
be `noexcept`. Strong-reference cycles still need weak links. Strong and weak counts
are 32 bits each in one atomic word and abort beyond 2^31 references per object,
the same order as the standard library's `int` counts.

MIT; the original [LICENSE](LICENSE) is preserved.

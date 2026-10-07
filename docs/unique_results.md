# Lean unique ownership: measured results and limits

## Final validated candidate and precision result

The final candidate selected for publication uses header SHA256
`17a1fdb0773ae287fc8955eecb629c20f7052c18324d1efd53554d4ed27cc90d`.
The earlier tables below describe initial lean header `4f13b3…`; they are retained
as historical measurements and are **not the final candidate's results**.

**The requested approximately 1% regression goal was not met.** The final candidate
reduces allocated-owner move/vector costs, but material residual costs and
measurement uncertainty remain. Optimization was stopped at the user's request.

The completed precision study retained **23,808 observations** across four
independent processes per header, with 31 paired blocks per workload, randomized
forward/reverse implementation order, approximately 25 ms observations and
process-local main/worker CPU affinity. All 48 diagnostic rows per header matched
checksums, lifetimes and handle sizes. No pool or global allocation replacement
was used. Original workload batch boundaries and counts were preserved.

Each value below is the final candidate's geometric mean ratio versus its
contract-matched standard owner, followed by a two-sided 95% interval over
independent process means. These are uncertainty estimates, not equivalence
proofs or application-speed claims.

| Workload | Default own8 / std8, 95% CI | Allocated own40 / erased std40, 95% CI |
|---|---:|---:|
| Create/read/destroy | 0.9768× [0.9652, 0.9886] | 1.0096× [0.9835, 1.0365] |
| Forced-observable move | 1.0085× [0.9927, 1.0245] | 1.5480× [1.5168, 1.5798] |
| Borrow/read | 0.9941× [0.9877, 1.0004] | 1.2416× [1.2273, 1.2560] |
| Frame draw batch | 1.0017× [0.9761, 1.0280] | 0.9957× [0.9905, 1.0010] |
| Vector owner transfer | 0.9929× [0.9655, 1.0210] | 1.1404× [1.0814, 1.2027] |
| Synchronized queue | 0.9635× [0.8791, 1.0560] | 1.0555× [1.0286, 1.0832] |

Relative to the initial lean build's within-binary matched-standard ratios, the
allocated move ratio fell from 1.9443× to 1.5480× and vector ratio from 1.5951× to
1.1404×. Cross-binary changes can also include code-placement effects. The
allocated handle remains 40 bytes; its public lifetime/deallocation contract is
preserved, including empty-before-callback reset behavior.

A/A controls prevent a credible all-workload 1% claim on this shared VM:
identical-code queue controls have wide intervals, and the identical std40 clone
creation ratio changes from 1.0595× in the initial binary to 0.9363× in the final
binary. Unchanged default-owner code also changes measured ratios between
binaries. Code layout and host effects remain unisolated explanations for
borrow-loop differences; the ~24% result cannot be attributed to extra borrow semantics.
Longer batches alone did not remove this uncertainty. No alignment or object-
padding experiment was run, and no unfavorable samples or cases were discarded.

Compact evidence: [final candidate ratios](../benchmarks/results/unique_precision/final_candidate.csv),
[all final A/A and owner rows](../benchmarks/results/unique_precision/candidate_summary.csv),
[initial lean controls](../benchmarks/results/unique_precision/baseline_summary.csv),
[metadata](../benchmarks/results/unique_precision/metadata.json), and
[reproduction source and runner](../benchmarks/precision/README.md).

## Ownership contracts

The default `own::unique_owner<T>` is now **8 bytes on this x86-64 ABI**, just like
ordinary `std::unique_ptr<T>`. It owns through typed `new T`/`delete T`, has no
reference count, control block, erased deleter or allocator metadata, and honors
class-specific allocation/deallocation. Qualification conversions are supported;
owning base conversions require accessible, unambiguous pointer conversion and
safe virtual destruction. Nonvirtual owning upcasts are rejected.

Explicit `own::allocate_unique<T>(allocator_ref, ...)` instead returns the separate
**40-byte `allocated_unique_owner<T>`**. It retains the original storage, allocator
context/callback, concrete destruction callback and exposed pointer. Both own
types borrow through the existing 8-byte `local_view`; the owner must remain alive
for the full borrowed scope.

The **rejected, unpublished 40-byte default-owner prototype** is measured from its
actual frozen header, not simulated using the new allocated type. Its complete
original snapshot, tests, reports and results were preserved without changes.
An exact header copy is [bundled for reproduction](../benchmarks/baseline_unique_erased/README.md).
The original 254-file snapshot passed its SHA256 manifest before and after timing.

## Historical initial lean results (header 4f13b3…)

GCC 14.2.0/libstdc++, Debian 13, Linux 6.18.44, AMD EPYC 9V74 cloud VM. Each cell is
**primary / independent-repeat median**. The old and lean columns come from
separate executable builds/processes. The standard column below comes from the
lean build; the old build's own standard controls are retained in the full CSV.

| Workload and unit | Actual old own, 40 B | Lean own, 8 B | Standard, 8 B |
|---|---:|---:|---:|
| Create/read/destroy, ns/resource | 10.834 / 10.886 | 8.925 / 9.247 | 9.152 / 9.493 |
| Forced-observable roundtrip, ns/two transfers | 11.391 / 11.771 | 1.132 / 1.134 | 1.115 / 1.129 |
| Isolated borrow/read, ns/read | 0.450 / 0.450 | 0.567 / 0.558 | 0.461 / 0.450 |
| Frame, µs/256 resources + 4,096 draws | 8.683 / 8.796 | 7.940 / 7.905 | 7.931 / 7.914 |
| Vector transfer, µs/1,024 owners + reads | 5.578 / 5.905 | 2.677 / 2.662 | 2.667 / 2.676 |
| Synchronized handoff, ns/job | 323.647 / 289.897 | 298.743 / 286.723 | 288.166 / 285.667 |

Within-round paired lean/std ratios were:

| Workload | Primary | Repeat |
|---|---:|---:|
| Create/read/destroy | 0.970× | 0.968× |
| Forced-observable roundtrip | 1.000× | 1.006× |
| Isolated borrow/read | 1.240× | 1.240× |
| Frame | 1.001× | 1.001× |
| Vector transfer | 1.001× | 0.997× |
| Synchronized handoff | 1.048× | 1.037× |

The lean design removes the large forced-state move penalty and is approximately
level with ordinary standard ownership in the tested frame/vector workloads.
Relative to actual old medians, the vector batch cost falls by **52.0% / 54.9%**
and frame cost by **8.6% / 10.1%**. These are cross-process median comparisons,
not paired old/new confidence estimates.

**Negative outcomes are retained:** two of six cases have higher lean/std paired
medians in both processes: the isolated borrow loop (about 24%) and synchronized
handoff (about 4–5%). Three cases are within approximately 1%; creation is about
3% lower, which is not established as an intrinsic advantage. The two borrow loops
have **identical generated instruction sequences** with different code addresses
and alignment. The slowdown is measured, but cannot be attributed to extra
`local_view` operations; code layout and host effects are plausible, unisolated
explanations. The threaded case is particularly scheduler-sensitive. There is no
overall speed score or general claim that this library beats `std::unique_ptr`.

## Why the forced move result is not an engine-speed claim

`observable_move_roundtrip` exposes both the populated and moved-from handles to
compiler memory barriers at both move boundaries. It deliberately forces the
entire handle state to be observable. The old owner clears all five source fields;
the standard erased deleter retains its moved-from metadata. Different state
requirements and generated checks/stores matter, not just handle size.

Conversely, the saved [code-generation check](../benchmarks/results/unique/codegen.txt)
shows that **unobserved roundtrip loops disappear entirely** for the actual old
prototype, lean own and standard ownership. All four inspected functions reduce
to two pointer/payload loads and return; the iteration argument is unused. This
is a qualitative compiler-elision result, not a near-zero nanosecond ownership
measurement. The optimized frame/vector cases do not put a barrier around each
owner move. A tenfold forced-state microbenchmark gap does not imply a tenfold
engine, frame or application improvement.

## Historical initial allocator/erased-contract costs

The new opt-in type is compared with `std::unique_ptr<T, erased_delete>`, whose
four-word stateful deleter plus pointer is also 40 bytes. It preserves the same
original storage, concrete destruction and allocator-context contract. This is
separate from the default 8-byte typed-delete contract. All allocator contexts
are null in these timings, using the unchanged default allocator callbacks.

| Workload and unit | New allocated own, 40 B | Erased standard, 40 B |
|---|---:|---:|
| Create/read/destroy, ns/resource | 10.517 / 10.445 | 9.198 / 9.595 |
| Forced-observable roundtrip, ns/two transfers | 11.039 / 11.271 | 5.742 / 5.819 |
| Isolated borrow/read, ns/read | 0.464 / 0.453 | 0.452 / 0.461 |
| Frame, µs/batch | 8.637 / 8.543 | 8.361 / 8.416 |
| Vector transfer, µs/batch | 5.619 / 5.714 | 3.328 / 3.513 |
| Synchronized handoff, ns/job | 305.017 / 298.557 | 292.987 / 302.195 |

Opt-in erased ownership still costs more in the forced moves and vector workload:
paired ratios versus erased standard are **1.960 / 1.948** and **1.664 / 1.574**,
respectively. The lean default does not make those opt-in costs disappear. Small
frame/borrow/threaded differences are not assigned statistical significance.

## Workload definitions and lifetime fairness

All implementations use the same 64-byte resource, initialization, payload reads,
object lifetimes, operation counts and independently computed expected checksum.
The timed code does not replace global allocation, instrument lifetimes, use a
pool, use mimalloc, change `local_view`, or cross threads with a local owner group.

- `create_read_destroy`: 50,000 complete resource lifecycles per sample. A handle
  escape barrier prevents allocation/handle state from simply disappearing
- `observable_move_roundtrip`: 500,000 roundtrips; each is two ownership transfers
  plus a read. The root's allocation and final destruction are outside timing
- `borrow_read_loop`: 1,000,000 fresh pointer-sized borrow/read iterations from a
  retained owner. Each borrow handle is exposed to the same compiler barrier
- `frame_draw_batch`: 64 frames. Each creates 256 owners, prepares 4,096 borrowed
  draw records, calls a consumer, drops the views, then destroys the resources
- `vector_owner_transfer`: 64 batches. Each moves 1,024 pre-existing owners into
  another reserved vector, clears the moved-from vector, prepares/reads 1,024
  views, discards the views and swaps the vector buffers for reuse. Resource
  allocation/destruction is outside timing; owner movement, view preparation
  and payload consumption are inside
- `synchronized_job_handoff`: 2,048 jobs through a 64-slot mutex/condition-variable
  queue to a persistent worker. The producer creates each resource and transfers
  its unique owner. The worker dequeues ownership **before** forming its borrow,
  sums all eight words, destroys the borrow and owner, then acknowledges completion.
  Thread creation is outside timing; object allocation, synchronization, transfers,
  payload work and destruction are included. Values are throughput-normalized
  batch averages, not individual job latency

Vectors reserve capacity before timing. The frame/vector consumer boundary makes
borrowed arrays and pointees observable, without forcing every intermediate
owner state. Raw pointers in standard cases and `local_view` in own cases have
identical retained-owner lifetime requirements. No borrower independently extends
lifetime or detects dangling access. Unobserved operations remain free to optimize
away, as they would in an optimized application.

## Validation, timing protocol and evidence

Timing began after implementation test attempts, 40 stress repetitions and
independent review probes had finished; none overlapped timing. See the separate
[test results](../tests/results/unique/README.md) for test/sanitizer status. Four
independent timing processes ran in counterbalanced order: frozen-primary,
lean-primary, lean-repeat, frozen-repeat. Each warmed every case five times, then
collected 51 samples per case/implementation. Case groups and implementation order
within each group were randomized with recorded seeds. The old build measures
three implementations, the lean build four; cross-build samples are not paired.

All **4,284 timed checksums** matched. The runner verifies sample counts, unique
sample keys, finite timings, adjacent randomized groups, matching counts/checksums
across every implementation/build, and 42 separate diagnostic rows. The diagnostic
build verifies exact resource construction/destruction counts and 8/40-byte owner,
8-byte borrow and 64-byte payload layouts. It does **not** count global allocations;
allocation correctness is tested separately by the implementation tests. The
timed payload destructor is trivial; only the untimed diagnostic adds counters.

For 256 owners, the owner array is 2,048 bytes for lean/ordinary standard versus
10,240 bytes for erased ownership. The 4,096-view array is 32,768 bytes in every
case. Each vector-transfer buffer holds 1,024 owners, so its capacity is 8,192 or
40,960 handle bytes; two such buffers are reserved. These are requested element
storage sizes, not allocator bookkeeping or whole-process memory measurements.

The summary reports each process separately. Paired ratios are medians of
within-round ratios, not ratios of aggregate medians. P10/P90/P95/P99 are linearly
interpolated quantiles of batch-average times, not per-operation latency tails or
confidence intervals. With 51 samples, the upper quantiles are especially coarse.

- [Raw samples](../benchmarks/results/unique/raw.csv)
- [Per-process summaries and paired ratios](../benchmarks/results/unique/summary.csv)
- [Layout, lifetime and checksum checks](../benchmarks/results/unique/checks.csv)
- [Unobserved moves and borrow-loop assembly](../benchmarks/results/unique/codegen.txt)
- [Minimal reproduction metadata and source hashes](../benchmarks/results/unique/metadata.txt)
- [Artifact SHA256 hashes](../benchmarks/results/unique/SHA256SUMS)

## Historical workload runner

Run the desired implementation tests first, wait for CPU-intensive checks to
finish, then explicitly supply the tested header's hash:

```sh
UNIQUE_BENCH_TESTED_HEADER_SHA256="$(sha256sum include/own/ownership.hpp | cut -d ' ' -f 1)" \
UNIQUE_BENCH_OUTPUT=benchmarks/results/my_unique_comparison \
scripts/benchmark_unique.sh
```

The runner uses the bundled frozen header by default and requires its exact
SHA256. `UNIQUE_ERASED_BASELINE` can select another directory containing that
same header. `UNIQUE_BASELINE_MANIFEST` optionally verifies a JSON manifest
against that directory; it is not needed for the bundled header. Our recorded run
used the original frozen snapshot and verified its full 254-file manifest.

Other controls are `CXX`, `UNIQUE_BENCH_CXXFLAGS`, `UNIQUE_BENCH_ROUNDS` (at least
31) and `UNIQUE_BENCH_WARMUPS` (at least 1). The default output is a new timestamped
directory; existing results are never overwritten by the runner. It needs a
C++20 GCC/Clang toolchain, GNU `objdump`, POSIX shell tools and Python's standard
library. Defaults are `-O3 -DNDEBUG -march=native -pthread -Wall -Wextra -Wpedantic`.
Only the small relevant assembly excerpts are saved, not complete disassemblies.

## Historical unpinned-run limits

These are CPU workload models on one shared Linux VM, not an engine/renderer/GPU
integration. There is no CPU pinning, exclusive physical host or frequency control.
Scheduler activity, virtualization, allocation state and compiler code layout can
affect results, especially the tiny borrow loop and threaded queue. No confidence
interval or causal decomposition is claimed for those historical unpinned runs.
The final precision study above adds pinned measurements and process-level intervals. Different compilers, platforms,
over-aligned/large/nontrivial resources, custom allocators, many workers, asset I/O,
virtual dispatch or real frames need their own measurements. Pointer-sized
ownership and zero reference counting establish neither universal superiority
nor an application-wide speedup.

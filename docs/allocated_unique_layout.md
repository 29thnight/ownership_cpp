# Allocator-aware exclusive ownership: handle versus allocation size

**Status: measurement only. The library is unchanged.** `allocated_unique_owner`
keeps its documented contract: a five-word handle, and an allocator that receives
exactly `sizeof(T)`/`alignof(T)`. This report records what changing that contract
would buy, for the owner to decide.

**Question.** `allocated_unique_owner` stores pointer, storage, allocator context,
deallocate and dispose in the handle (40 bytes). The upstream precision study
found it slower than its contract-matched standard owner on moves and vector
transfers. Would moving the cleanup state into a header at the front of the
allocation (a 16-byte handle: payload pointer and header pointer) remove that?

The prototype (`proto::owner` in
[the benchmark source](../benchmarks/allocated_unique_layout.cpp)) stores
`{context, deallocate, dispose}` (24 bytes) before the payload; its disposal
function knows the concrete type, so base conversions would still destroy and
free the original allocation. The allocator then receives
`sizeof(header) + sizeof(T)` rounded to alignment instead of `sizeof(T)`.

## Machine and method (seven fields)

1. **CPU:** Intel Xeon Processor @ 2.30GHz (cloud VM, x86-64), Linux 6.18.44
2. **Cores used:** one, pinned with `taskset -c 0`
3. **Frequency:** 2300 MHz nominal; governor and turbo not visible from the guest.
   SMT: 1 thread per core as reported
4. **Compiler:** GCC 13.3.0, `-std=c++20 -O2 -DNDEBUG -pthread -Wall -Wextra -Wpedantic`
5. **Workload:** a 32-byte payload, default allocator.
   `create_read_destroy` (400,000 objects); `move_chain` (one owner moved through
   64 observable slots, 20,000 rounds); `vector_transfer_4096` (4,096 owners moved
   between two reserved vectors and back, 101 rounds); `owner_scan_4096` (payload
   reads through a vector of 4,096 owners, 401 passes)
6. **Baseline:** `std::unique_ptr` with upstream's five-word erased deleter (twice,
   as an A/A control) and plain `std::unique_ptr<T>` as the one-word floor
7. **Method:** `steady_clock`, 2 discarded warmups, 15 samples, seeded shuffled case
   order, median with min, p90 and cv; checksums verified

## Results

Median ns per operation, primary / repeat:

| Case | own 40 B (current) | prototype 16 B | std erased 40 B (A / B) | std default 8 B |
|---|---:|---:|---:|---:|
| create_read_destroy | 11.31 / 11.15 | 10.26 / 10.12 | 10.38 / 10.10, 10.91 / 10.07 | 9.95 / 9.53 |
| move_chain | 2.13 / 2.10 | 2.37 / 1.94 | 1.83 / 1.79, 1.81 / 1.78 | 0.84 / 0.80 |
| vector_transfer_4096 | 2.68 / 2.68 | **1.64 / 1.46** | 3.24 / 3.13, 3.20 / 3.15 | 2.29 / 2.27 |
| owner_scan_4096 | 0.495 / 0.487 | 0.467 / 0.435 | 0.489 / 0.477, 0.478 / 0.476 | 0.401 / 0.397 |

| Per object | own 40 B | prototype 16 B |
|---|---:|---:|
| Handle | 40 B | 16 B |
| Allocator request (32 B payload) | 32 B | 56 B |
| glibc chunk | 48 B | 64 B |

Data: [primary](../benchmarks/results/allocated_unique_layout_primary/summary.csv),
[repeat](../benchmarks/results/allocated_unique_layout_repeat/summary.csv).

## Analysis

The smaller handle pays off where handles are copied in bulk: moving 4,096 owners
between vectors costs 39-45% less (2.68 to 1.64/1.46 ns per move), below even the
one-word `std::unique_ptr` in this loop. Creation is 9% cheaper and scans through
a vector of owners 5-10% cheaper, since the vector holds 16 instead of 40 bytes
per handle.

It does not fix single moves: `move_chain` moved within run-to-run noise
(2.13/2.10 to 2.37/1.94 ns). Notably the current own handle is slower here than
the standard erased owner of the same 40-byte size (about 2.1 vs 1.8 ns), so that
gap comes from the move-assignment implementation (construct a temporary, then a
swap with emptiness branches), not from the handle size. That is a separate,
contract-preserving optimization worth trying first.

The cost is 24 more bytes per allocation (one more glibc size class here, 48 to
64 bytes, +33%) and a changed allocator contract: custom allocators would see
header-inclusive sizes, and upstream's tests that assert `sizeof(T)` requests
and a five-word handle would change.

## Recommendation

Keep the current contract unless vector or container transfers of allocated
owners dominate a real workload. First try a contract-preserving move assignment
for the five-word handle and re-run this benchmark; adopt the header layout only
if bulk transfers still matter after that.

## Reproduce

```sh
scripts/benchmark_allocated_unique.sh
QUICK=1 scripts/benchmark_allocated_unique.sh
```

# Control-block layout

**Claim.** Shrinking the shared control block lowers the bytes allocated per
object and speeds up scans over many owned objects, but places the payload on the
same cache line as the reference counts, so a thread reading a hot payload while
other threads copy its owners pays for the line moving.

**Change measured.** The block header went from nine words (two counts, a
three-word `allocator_ref`, a two-word `retirement_hook`, `dispose`, `destroy`) to
three (two counts and a pointer to a static operations table). `make_shared` and
`make_local` use a compact block without allocator or hook state; `allocate_*` and
`*_with` factories use an extended block. Local groups dropped the unused allocate
callback (48 to 40 bytes).

*Later revision:* packing both counts into one word took the compact block to 24
bytes (glibc chunk 32), the same as libstdc++; see
[the count-layout report](benchmark_counts.md). The measurements below are for the
32-byte compact block.

| Per `make_shared<8-byte payload>` | Requested bytes | glibc chunk |
|---|---:|---:|
| Previous layout | 80 | 96 |
| Compact block | 32 | 48 |
| libstdc++ `std::make_shared` | 24 | 32 |

## Machine and method (seven fields)

1. **CPU:** Intel Xeon Processor @ 2.30GHz (cloud VM, x86-64), Linux 6.18.44
2. **Cores used:** one for create and scan cases; four (one reader, three copiers)
   for the reader case. No pinning
3. **Frequency:** 2300 MHz nominal; governor and turbo not visible from the guest.
   SMT: 1 thread per core as reported
4. **Compiler:** GCC 13.3.0, `-std=c++20 -O2 -DNDEBUG -pthread -Wall -Wextra -Wpedantic`
5. **Workload:**
   - `create_read_destroy`: 400,000 × `make_shared`, read, drop
   - `scan_16k_objects` / `scan_256k_objects`: payload reads through 16,384 or
     262,144 owners created back to back, visited in a fixed shuffled order
     (65 or 9 passes)
   - `reader_with_copiers`: one thread performs 2,000,000 dependent payload
     reads through its own owner while three threads copy and drop owners of
     the same object; timing starts after every copier has made progress, and a
     window in which any copier made none is discarded and repeated. Each sample
     averages eight objects allocated back to back, because whether counts and
     payload share a line depends on the allocation's offset within a line
   - `own_a64`: the same cases with an `alignas(64)` payload
6. **Baseline:** `std::make_shared` in the same binary, run twice (`std_a`,
   `std_b`) as an A/A control
7. **Method:** `steady_clock`, 2 discarded warmups, 15 samples, seeded shuffled
   case order per round, median with min, p90 and cv; checksums verified

Each header was measured in two independent processes.

## Results

Median ns per operation, primary / repeat:

| Case | Implementation | Previous layout | Compact block |
|---|---|---:|---:|
| create_read_destroy | own | 20.39 / 20.29 | 21.93 / 22.16 |
| create_read_destroy | std_a / std_b | 11.59 / 11.59, 11.71 / 11.41 | 11.45 / 11.69, 11.54 / 11.57 |
| scan_16k_objects | own | 1.226 / 1.253 | **0.942 / 0.898** |
| scan_16k_objects | std_a | 0.879 / 0.869 | 0.904 / 0.862 |
| scan_256k_objects | own | 8.73 / 8.32 | **7.65 / 7.03** |
| scan_256k_objects | std_a | 6.68 / 6.52 | 6.86 / 6.28 |
| reader_with_copiers | own | 0.283 / 0.278 | **5.06 / 5.40** |
| reader_with_copiers | own_a64 | 0.277 / 0.274 | 0.266 / 0.265 |
| reader_with_copiers | std_a / std_b | 1.29 / 1.59, 1.44 / 1.39 | 1.71 / 1.64, 2.08 / 1.92 |
| scan_16k_objects | own_a64 | 7.82 / 8.06 | 1.335 / 1.278 |
| scan_256k_objects | own_a64 | 11.74 / 11.87 | 10.38 / 9.79 |

Full data: [previous primary](../benchmarks/results/layout_wide_header_primary/summary.csv),
[previous repeat](../benchmarks/results/layout_wide_header_repeat/summary.csv),
[compact primary](../benchmarks/results/layout_compact_header_primary/summary.csv),
[compact repeat](../benchmarks/results/layout_compact_header_repeat/summary.csv),
each with raw samples, sizes, metadata and hashes.

## Analysis

The compact block halves the allocator chunk per object (96 to 48 bytes) and
makes shuffled scans 23-28% faster for 16k objects and 12-16% faster for 256k
objects, close to `std::make_shared`. The scan gain is the payloads being denser:
fewer distinct lines and pages per object visited.

The cost appears exactly where predicted. With the previous 72-byte header the
payload always started on a later line than the counts, so a reader never saw
the copiers' writes (0.28 ns per read, an L1 hit). With a 24-byte header the
payload shares the counts' line for most allocation offsets, and the reader's
loads miss whenever a copier has just taken the line: about 5 ns per read. The
standard library has the same layout and shows the same effect at 1.3-2.1 ns;
own's copiers run faster since the single-instruction increment, which moves the
line more often. An `alignas(64)` payload restores 0.27 ns in either layout and,
with the compact header, costs far less footprint than before (one 128-byte
block instead of 192).

`create_read_destroy` is 8% slower with the compact block (about 1.7 ns): dispose
and destroy are now reached through the operations table, one more dependent
load. Both layouts remain about 9 ns slower than `std::make_shared`; that gap is
the two locked read-modify-writes own performs on the last release, which the
standard library skips when no other reference can exist (addressed separately).

The A/A control agrees: identical `std_a` and `std_b` code differ by less than
the effects reported, except in the reader case, where std's two copies range
from 1.3 to 2.1 ns between processes, so only differences larger than that are
claimed there.

## Limits

- One allocator (glibc malloc) decides offsets; another allocator or a pool with
  64-byte alignment changes how often counts and payload share a line
- One 4-vCPU VM without pinning; no PMU access, so line transfers are inferred
  from timing and the controls, not counted
- The reader case is a worst case: three cores copying owners of one object
  continuously while a fourth reads it

## Reproduce

```sh
scripts/benchmark_layout.sh                    # full run
QUICK=1 scripts/benchmark_layout.sh            # smoke test
OWN_INCLUDE=/path/to/other/include scripts/benchmark_layout.sh
```

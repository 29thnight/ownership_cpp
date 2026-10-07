# Reference-count word layout and release ordering

**Claim.** Destroying a never-shared `make_shared` object cost own two locked
read-modify-writes (the strong release, then the implicit weak release), where
libstdc++ needs none. Packing both counts into one word lets the last owner learn
from the strong decrement's own result that no weak observer exists, so the weak
release becomes a plain load. A second way to remove both read-modify-writes,
loading the word before every decrement as libstdc++ does, was measured and
rejected because the extra load costs more on copies than it saves on
destruction.

**Changes measured.**

- *Split counts* (previous): separate 64-bit strong and weak atomics; every
  decrement `acq_rel`.
- *Packed, pre-check* (variant A, rejected): one 64-bit word with 32-bit halves;
  each strong release first loads the word and skips both read-modify-writes when
  it reads exactly one strong and one implicit weak.
- *Packed* (adopted): the same word; no load before the decrement. A `release`
  `fetch_sub`, an acquire load only after the last strong release, and a weak
  release that is one acquire load when no weak observer exists.

The header shrinks from 24 to 16 bytes: `make_shared<8-byte payload>` requests 24
bytes, as libstdc++ does (glibc chunk 32 instead of 48).

## Machine and method (seven fields)

1. **CPU:** Intel Xeon Processor @ 2.30GHz (cloud VM, x86-64), Linux 6.18.44
2. **Cores used:** up to all 4 online vCPUs, one socket, one NUMA node, **pinned**:
   `--pin 1` places worker t (scaling) or copier c (layout) on its own CPU
3. **Frequency:** 2300 MHz nominal; governor and turbo not visible from the guest.
   SMT: 1 thread per core as reported
4. **Compiler:** GCC 13.3.0, `-std=c++20 -O2 -DNDEBUG -pthread -Wall -Wextra -Wpedantic`
5. **Workload:** the [scaling](benchmark_scaling.md) and [layout](benchmark_layout.md)
   harnesses unchanged in method, now with pinning and, in the scaling harness, a
   second identical std implementation
6. **Baseline:** `std::shared_ptr` twice in the same binary (A/A control)
7. **Method:** `steady_clock`, 2 discarded warmups, 15 samples, seeded shuffled case
   order, median with min, p90 and cv; every sample verified

The previous and adopted headers each ran in two processes; variant A in one.

## Results

Median ns, previous primary / repeat, variant A, adopted primary / repeat:

| Case | Previous | Variant A | Adopted |
|---|---:|---:|---:|
| create_read_destroy, own | 21.76 / 21.82 | **12.53** | **17.97 / 17.77** |
| create_read_destroy, std_a | 11.48 / 11.65 | 11.49 | 12.14 / 11.46 |
| shared_copy_drop, own, 1 thread | 13.30 / 13.02 | 16.25 | 13.61 / 14.01 |
| private_copy_drop, own, 1 thread | 13.23 / 12.86 | 16.30 | 14.06 / 14.17 |
| shared_copy_drop, own, 2 threads | 107.68 / 99.14 | 143.67 | 103.14 / 114.48 |
| shared_copy_drop, own, 4 threads | 238.36 / 221.47 | 303.21 | 220.91 / 231.06 |
| shared_copy_drop, std / std_b, 4 threads | 349.50 / 378.06 | 299.67 / 303.01 | 326.12 / 322.43 |
| weak_lock_drop, own, 4 threads | 387.91 / 376.60 | 335.49 | 361.78 / 364.62 |
| reader_with_copiers, own | 4.70 / 4.86 | 2.16 | 5.07 / 4.56 |
| scan_16k_objects, own | 0.89 / 0.95 | 0.87 | 0.86 / 0.85 |

Data: scaling [previous](../benchmarks/results/scaling_split_counts_primary/summary.csv)
([repeat](../benchmarks/results/scaling_split_counts_repeat/summary.csv)),
[variant A](../benchmarks/results/scaling_packed_precheck_primary/summary.csv),
[adopted](../benchmarks/results/scaling_packed_counts_primary/summary.csv)
([repeat](../benchmarks/results/scaling_packed_counts_repeat/summary.csv)); layout
[previous](../benchmarks/results/layout_split_counts_primary/summary.csv)
([repeat](../benchmarks/results/layout_split_counts_repeat/summary.csv)),
[variant A](../benchmarks/results/layout_packed_precheck_primary/summary.csv),
[adopted](../benchmarks/results/layout_packed_counts_primary/summary.csv)
([repeat](../benchmarks/results/layout_packed_counts_repeat/summary.csv)).
Codegen: [packed_counts_codegen.txt](../benchmarks/results/packed_counts_codegen.txt).

## Analysis

The adopted layout makes creating and destroying a never-shared object 18% cheaper
(21.8 to 17.9 ns) without measurably changing copies: single-thread and contended
copy/drop stay within the A/A spread of the std rows. The remaining 6 ns gap to
`std::make_shared` is the one locked decrement own still performs.

Variant A reaches parity with the standard library on creation (12.5 ns) because
it performs no read-modify-write at all for a never-shared object. The price is a
load before every decrement: 24% more per uncontended copy/drop pair (13.3 to
16.3 ns) and 31-40% more under contention (two threads 107.7 to 143.7 ns, four
threads 238.4 to 303.2 ns), which brings own back to the standard library's
contended cost, as the [scaling report](benchmark_scaling.md) suspected of
libstdc++. Shared ownership exists to be copied, and `unique_owner` already
creates and destroys an unshared object without any atomic operation, so the
adopted layout keeps the copy path fast. Variant A's lower reader cost (2.2 ns)
follows from its slower copiers moving the line less often, not from layout.

The release ordering change cannot show on this x86 machine, where every locked
instruction is a full barrier. The codegen record shows the effect on Arm with
LSE atomics: the previous header emitted `ldaddal` (acquire and release) for
both decrements of a never-shared object; the adopted header emits one `ldaddl`
(release), an `ldar` only after the last strong release, and an `ldar` compare
instead of the weak `ldaddal`.

## Limits

- One 4-vCPU x86 VM; pinning removes migration but not hypervisor scheduling.
  The std A/A pair differs by up to 9% in contended rows, which bounds the
  differences claimed there
- Arm codegen was compiled, not run: no Arm timing is claimed
- Counts are now limited to 2^31 strong and 2^31 weak references per object
  (aborting beyond), the same order as the standard library's `int` counts

## Reproduce

```sh
scripts/benchmark_scaling.sh --pin 1
scripts/benchmark_layout.sh --pin 1
OWN_INCLUDE=/path/to/other/include scripts/benchmark_scaling.sh --pin 1
```

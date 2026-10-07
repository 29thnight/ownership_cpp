# Exact prior benchmark source

`ownership_bench.cpp` is byte-for-byte from published commit
`ae6caf8334c6e1b2777d03291065513b5eadeeb7` and retains all 37 prior cases.
SHA-256: `cafb96ef1d60afc4fe251057a3fc053772d31fbd3105e5f1a453e5bf0884feae`.

The old published header and script were actually compiled/run from a separate
Git checkout at that exact commit. See `../results/20261007_ae6caf8_rerun_primary`
and `../results/20261007_ae6caf8_rerun_repeat` for those fresh measurements.
This is not an emulated baseline or a source port onto the new implementation.

For the paired current-header control, the unchanged source here is compiled
against the current header with the same compiler and flags:

```sh
BENCH_SOURCE=benchmarks/baseline_ae6caf8/ownership_bench.cpp \
BENCH_OUTPUT=benchmarks/results/my_current_header_control \
scripts/benchmark.sh --samples 101 --warmups 5 --iterations 100000
```

The full current benchmark adds view-first cases without removing the original
37 cases. An unchanged-harness control helps distinguish those new comparisons
from historical ownership measurements; CPU scheduling and code layout still
limit tiny timing comparisons.

The original published report is preserved exactly in
[`docs/benchmark_results_ae6caf8.md`](../../docs/benchmark_results_ae6caf8.md), and
its original result directories have not been rewritten.

# Unique-owner precision reproduction

This is the exact workload/driver used for the final-candidate 25 ms study. It compares final header `17a1fdb…` with bundled initial lean header `4f13b3…`; it is separate from the rejected erased-default prototype comparison. Source hashes and compact results are in `../results/unique_precision/`.

Requirements: Linux, GCC-compatible C++20 compiler, Python 3 with NumPy and SciPy. No dependency installation is performed. The shell runner is a reproduction convenience; the recorded run used the same compile commands and ABBA schedule. The benchmark driver and workload source are unchanged copies of the measured files.

Run tests first and do not run CPU-heavy checks concurrently. Choose two permitted logical CPUs using `UNIQUE_PRECISION_MAIN_CPU` and `UNIQUE_PRECISION_WORKER_CPU` (defaults 2 and 3). Then:

```sh
UNIQUE_PRECISION_TESTED_HEADER_SHA256="$(sha256sum include/own/ownership.hpp | cut -d ' ' -f 1)" bash benchmarks/precision/run.sh
```

The runner refuses changed headers or an existing result directory. `UNIQUE_PRECISION_OUTPUT`, `CXX`, and `UNIQUE_PRECISION_CXXFLAGS` are optional overrides; any changed flags define a different experiment. No uniform-alignment or object-padding experiments were run.

Eight implementations include same-code std8/std40 instance controls, separate std clone instantiations, and the two own types. Every block randomizes implementation order then reverses it. Original batches are repeated to target 25 ms with a common repetition count per case. Every observation is retained. The main/queue-worker threads use process-local CPU affinity. This does not reserve the physical host or fix frequency.

The primary ratio is an equal-process-weight geometric mean of paired forward/reverse block ratios. The 95% interval uses Student t over the four independent process means of log ratios. Adjacent raw samples are not treated as independent. A one-sided bound additionally adjusts for twelve own/std comparisons. Optional moving-block bootstrap analysis is available but was not used for the reported primary intervals.

The roughly 1% goal was **not met**. A/A controls expose noise and code-placement sensitivity, while allocated-owner move/vector costs remain larger. Inspect all control rows; do not select favorable medians, padding, or only successful cases.

# Validation record: lean default and allocator-aware unique owners

Date: 2026-10-07. Linux 6.18.44 x86_64; GCC 14.2.0 (Debian
14.2.0-19). C++20, pthreads, `-Wall -Wextra -Wpedantic -Werror`.

Tested header SHA-256:
`17a1fdb0773ae287fc8955eecb629c20f7052c18324d1efd53554d4ed27cc90d`.
`source-sha256.txt` identifies the header, runner, and all test sources.
Every recorded full run below began after that header was frozen. These
records validate the pointer-only default `unique_owner<T>` and separate
`allocated_unique_owner<T>`, then rerun every existing regression test.
Earlier published records elsewhere under `tests/results/` are unchanged.
The unpublished erased-default prototype is a separate frozen baseline and
is not the implementation validated by these records.

## Commands and results

The completed runs used `CXX=g++` and
`OWN_TEST_BUILD_DIR="$PWD/build/perf_iterations/candidate_001_tests/bin"`.

| Command | Result | Log |
| --- | --- | --- |
| `./scripts/test.sh debug` | Passed, exit 0 | `debug.log` |
| `./scripts/test.sh release` | Passed, exit 0 | `release.log` |
| `./scripts/test.sh ubsan` | Passed, exit 0 | `ubsan.log` |
| `./scripts/test.sh tsan` | Passed, exit 0 | `tsan.log` |
| `ASAN_OPTIONS=detect_leaks=0 ./scripts/test.sh asan` | ASan + UBSan passed, exit 0; leak checking disabled | `asan-detect-leaks-0.log` |
| `OWN_TEST_BUILD_DIR="$PWD/build/perf_iterations/candidate_001_tests/bin-asan-default" env -u ASAN_OPTIONS ./scripts/test.sh asan` | Environment-blocked at the first executable, exit 1 | `asan-default.log` |
| Five additional runs each of `ownership_tests`, `owner_from_this_tests`, `unique_owner_tests`, and `allocated_unique_owner_tests` in release and TSan | All 40 passed | `repeated-concurrency.log` |

Default LeakSanitizer was retried and explicitly reports that it does not work
under ptrace. This is not a successful leak-sanitizer run. Exact custom-allocator
accounting and dedicated global-new fault injection verify the exercised
allocation paths; they do not replace whole-program leak checking. Clang was
unavailable; other compilers, operating systems, architectures, and standard
libraries were not tested. Public logs omit absolute workspace paths and
process IDs.

## Coverage

Every completed mode passes 107 runtime cases: the original 52 plus 16 lean
unique-owner cases, 28 allocator-aware unique-owner cases, and 11 dedicated
allocation cases. Header isolation and multi-translation-unit creation,
const conversion, transfer, borrowing, and destruction pass for both owner
types. The opaque default-owner declaration and pointer-sized representation
compile while its pointee is incomplete. Empty allocator-aware ownership can
be moved, reset, and destroyed even when its pointee is never defined.

Lean default ownership checks include:

- Exactly one pointer of handle storage, pointer alignment, move-only and
  nonthrowing handle operations, no raw construction/adoption/reset/release,
  no conversion to shared ownership, and no allocator-aware interconversion
- Same-type and safe const transfers; public, unambiguous owning upcasts only
  through accessible `noexcept` virtual destructors; nonvirtual base borrowing
  remains allowed
- Nonzero-offset virtual-base conversion, chained base/const conversion, and
  deletion through a base selecting the derived class's allocation/deletion
  pair with the original allocation address
- Ordinary typed `new T`/`delete T`: class-specific new and delete for mutable
  and const payloads, sized class delete, aligned class new/delete, and
  matching constructor-failure cleanup; a class `noexcept` new returning null
  yields an empty owner according to normal new-expression semantics
- Exactly one global allocation for ordinary, over-aligned, and const
  payloads; injected allocation failure and constructor unwinding; no extra
  allocation for empty handles, moves, or borrowed views
- Perfect forwarding, scalar value initialization, reset, self-move/swap,
  nested-source move assignment, vector relocation, and exact-once destruction
- Empty observation and safe reentrant reset during explicit `reset()`, and
  a destructor-installed replacement preserved by that reset; no claim that
  a handle being destructed itself is observable as empty
- Synchronized cross-thread transfer and destruction; unbound
  `enable_owner_from_this`, without shared/weak registration or metadata

Allocator-aware ownership retains the former erased-owner behavior: a
40-byte handle on this ABI, one payload-sized/aligned byte allocation,
original concrete destructor and allocation provenance across nonvirtual,
multiple, and virtual base conversions, matching context/size/alignment on
free, null/throwing allocation failure, constructor unwinding, const and
aligned payloads, and explicit global placement-new semantics that bypass
class-specific new/delete. Its tests also retain self-move, allocator-aware
swap, nested-source assignment, reentrant reset, destructor-installed
replacement, unbound from-this, and synchronized cleanup on another thread.
The allocator context remains alive through the last callback in every test;
these handles do not extend a caller-owned context's lifetime. Four additional
cases exercise empty moved-from/reset handles after both the former payload and
heap allocator context have died, including moves, swaps, replacement and
const/virtual-base conversions. Only valid public empty states are inspected.
Independent source review verified that inactive cleanup metadata is never read;
runtime sanitizers on this ABI cannot establish that invariant alone.

Debug additionally passes all 21 existing expected-abort cases and 125 compile
rejections: 48 existing and 77 unique-owner-related. Both owner types reject
copying, raw/reference adoption, raw reset/release/get, arbitrary deleters,
standard-allocator misuse, arrays, throwing destructors, const removal,
unsafe downcasts, private/ambiguous bases, rvalue borrowing/raw escape, and
unsupported conversions to or from shared/local/weak/view/standard-unique
ownership. Additional rejections cover default nonvirtual owning upcasts and
assignments, inaccessible virtual destruction, incomplete default destruction,
deleted class new/delete, interconversion between the two owner types, and
an allocator deallocation callback that is not `noexcept`.

All 12 exact deprecation-diagnostic checks pass: 8 existing plus each unique
owner's `unsafe_get()` with its default setting and explicit `=1`. Numeric
`=0` compiles and runs both escape/signature checks. Default and
warning-disabled multi-translation-unit links pass with consistent macro
settings. Normal owner/view access remains warning-clean. API rejection tests
disable only the intentional raw-escape warning and unused-variable
diagnostics, so deprecation cannot falsely satisfy a rejection.

The tests never concurrently mutate a single handle or access an expired
borrowed view. They validate this contract, not general C++ lifetime safety.
No dependencies, allocator pools, or changes to the shared/local/weak
allocation strategy were introduced by this validation work.

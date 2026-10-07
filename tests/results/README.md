# Validation record

Date: 2026-10-07. Environment: Linux 6.18.44 x86_64, GCC 14.2.0
(Debian 14.2.0-19). All builds use C++20, pthreads, and
`-Wall -Wextra -Wpedantic -Werror`.

The implementation tested has SHA-256
`f2a8a71b1bdf30311618a92aa657d01b39459290df6a88d0a49c1b9c6f3f6c88`.
`source-sha256.txt` records the header, runner, and test source hashes.

## Final commands and results

| Command | Result | Log |
| --- | --- | --- |
| `./scripts/test.sh debug` | Passed, exit 0 | `debug.log` |
| `./scripts/test.sh release` | Passed, exit 0 | `release.log` |
| `./scripts/test.sh ubsan` | Passed, exit 0 | `ubsan.log` |
| `./scripts/test.sh tsan` | Passed, exit 0 | `tsan.log` |
| `ASAN_OPTIONS=detect_leaks=0 ./scripts/test.sh asan` | AddressSanitizer + UBSan passed, exit 0; leak checking disabled | `asan-detect-leaks-0.log` |
| `./scripts/test.sh asan` | Environment-blocked, exit 1 at first executable | `asan-default.log` |
| Five additional executions each of `build/tests/release/ownership_tests` and `build/tests/tsan/ownership_tests` | All 10 passed, 29 cases each | `repeated-concurrency.log` |

The default ASan run reports: “LeakSanitizer does not work under ptrace
(strace, gdb, etc)”. This is not a successful leak-sanitizer run. Custom
allocator accounting and the separate global-new fault-injection tests verify
allocation/deallocation balance on the tested paths, but do not replace a
working whole-program leak sanitizer. Clang, MSVC, other operating systems,
other CPU architectures, and standard-library implementations were not tested.

Each completed mode includes:

- 29 runtime tests, including ownership transitions and alias counts; const,
  multiple-inheritance, and virtual-base conversions; exact-once destruction;
  over-alignment; throwing constructors; custom allocator failures; weak locking
  and final-release races; independent local groups; deferred retirement,
  cancellation, move assignment, and migration; embedded weak references;
  class-specific operator-new hiding
- 3 standalone tests replacing global new to inject real default allocator
  failures into `make_shared`, both `make_local` allocations, and lvalue/rvalue
  `localize`, with unchanged-owner and allocation-balance checks
- A self-contained header include and operations on handles to an incomplete
  type, plus a three-translation-unit compile/link/run

Debug additionally includes 20 expected-abort checks for cross-thread local
operations, destruction, counter overflow, forbidden zero-count increments,
and invalid allocator callbacks; and 11 expected compilation failures for raw
ownership adoption, arrays, implicit promotion, const removal, and a throwing
payload destructor. Overflow cases are intentionally white-box tests of the
internal counter helpers, rather than corruption of actual ownership handles.

`initial-all.log` is an earlier exploratory run, retained only as history; use
the per-mode logs above for the final test suite. The default test runner
does not silently skip a failing requested sanitizer.

## Independent implementation review

Review covered strong/weak count release ordering, the implicit weak reference
through delayed retirement, source preservation on allocation failure,
matching allocation size/alignment, concrete payload destruction through base
owners, virtual-base weak conversions during expiry, count overflow checks,
thread-affinity enforcement, and header ODR behavior. No remaining defect was
identified in the documented supported contract. This is test/review evidence,
not a proof that every possible execution is correct.

# Validation record: borrowed views and owner-from-this

Date: 2026-10-07. Environment: Linux 6.18.44 x86_64, GCC 14.2.0
(Debian 14.2.0-19). Builds use C++20, pthreads, and
`-Wall -Wextra -Wpedantic -Werror`.

The final tested header has SHA-256
`896f997b8c5ca0a479ecb21f5c1463510bd7ef64ee4569463c9726ab07cc4e9d`.
`source-sha256.txt` records the header, runner, and every test source hash.

This revision centers `shared_owner`, pointer-sized non-owning `local_view`,
`weak_owner`, and factory-bound `enable_owner_from_this`. The optional owning
`local_owner` retains its existing two-allocation factory behavior. No
embedded-group/coallocation optimization is included in this revision.

## Final commands and results

| Command | Result | Log |
| --- | --- | --- |
| `./scripts/test.sh debug` | Passed, exit 0 | `debug.log` |
| `./scripts/test.sh release` | Passed, exit 0 | `release.log` |
| `./scripts/test.sh ubsan` | Passed, exit 0 | `ubsan.log` |
| `./scripts/test.sh tsan` | Passed, exit 0 | `tsan.log` |
| `ASAN_OPTIONS=detect_leaks=0 ./scripts/test.sh asan` | AddressSanitizer + UBSan passed, exit 0; leak checking disabled | `asan-detect-leaks-0.log` |
| `OWN_TEST_BUILD_DIR="$PWD/build/tests/asan-default-check" ./scripts/test.sh asan` | Environment-blocked, exit 1 at first executable | `asan-default.log` |
| Five additional executions each of `ownership_tests` and `owner_from_this_tests`, in both release and TSan | All 20 passed | `repeated-concurrency.log` |

The default ASan run was retried for this revision and reports:
“LeakSanitizer does not work under ptrace (strace, gdb, etc)”. This is not a
successful leak-sanitizer run. Custom allocator accounting and separate
process-wide allocation fault injection verify allocation/deallocation
balance on tested paths, but do not replace whole-program leak checking.
Clang was not installed. Clang, MSVC, other operating systems, other CPU
architectures, and other standard-library implementations were not tested.

## Coverage

Every completed build mode includes a self-contained header/incomplete-type
smoke test and a three-translation-unit compile/link/run, followed by:

- 29 original ownership/runtime cases, covering local/shared transitions,
  adjusted pointers, const and virtual bases, exact-once destruction,
  over-alignment, allocation and construction failure, optional independent
  local groups, weak final-release races, and deferred retirement
- 8 borrowed-view cases, plus static interface assertions: pointer-sized and
  trivially copyable/destructible representation, retained-owner function
  parameters and loops, no ownership or counter changes, const and safe base
  conversions, one explicit strong copy when a source may reset, weak-lock
  usage, no payload lifetime extension, and deferred retirement unaffected by
  views. Tests never inspect or dereference an expired borrowed view
- 11 owner-from-this cases: unmanaged/constructor/destructor states, same
  original control block, optional local-owner factories, const factories,
  copy/move fresh registration, assignment preserving destination identity,
  nonzero-offset/virtual-base/virtual-diamond inheritance, deferred expiry,
  failure after registration, constructor failure, concurrency, and final
  destruction on another thread
- 4 dedicated global-new fault-injection cases, including the unchanged two
  `make_local` allocations and proof that ordinary view creation/copy/access
  makes no allocation attempts

Debug additionally includes:

- 21 expected-abort cases, including cross-thread local `borrow()` and
  `unsafe_get()`, other local-owner operations, overflow, and invalid callbacks
- 48 expected compilation failures, including removed `get()`, raw/reference
  view construction, raw ownership/reset/implicit pointer conversion,
  view-to-owner conversion, const removal, owner rvalue borrow/raw escape,
  rvalue from-this view access, weak direct access, arrays, throwing payload
  destructors, and private/protected/ambiguous/incompatible from-this mixins
- 8 exact deprecation-diagnostic checks: `unsafe_get()` on shared/local owners,
  lvalue views, and temporary views, both with the default configuration and
  explicit `OWN_ENABLE_UNSAFE_GET_WARNING=1`
- The same 4 raw-escape programs compiled/run with numeric warning setting
  `0`, explicit escape-hatch signature/ref-qualification assertions, and a
  warning-disabled multi-translation-unit link with one consistent setting

Normal owner/view dereference and arrow operations, including
`weak.lock()->member`, compile with deprecation warnings enabled and
`-Werror`. Compile-rejection fixtures disable only the intentional raw-escape
warning (and unused-variable warnings), so deprecation cannot falsely pass a
rvalue/ownership rejection test. Thread-affinity death tests likewise disable
only the raw-escape warning for their intentional `unsafe_get()` invocation.

The test runner never silently skips a failing requested sanitizer. Runtime
and compiler checks are evidence for the supported contract, not a proof of
all executions or a C++ lifetime/borrow checker.

## Baseline archive

All previous records are preserved unchanged under
[`baseline-ae6caf8/`](baseline-ae6caf8/). Those files describe the pre-view
baseline at commit `ae6caf8334c6e1b2777d03291065513b5eadeeb7` and its earlier
header hash. They are historical evidence, not validation of this revision.
Use the top-level per-mode logs listed above for the current code.

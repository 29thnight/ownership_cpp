# ownership_cpp

Header-only C++20 ownership handles in namespace `own`, with a cheap thread-local
copy path and explicit cross-thread ownership boundaries.

This is an independent reference-counting implementation. The production header
uses low-level standard facilities (`atomic`, type traits, utility, allocation,
byte sizes, and fail-fast termination); it does **not** use `std::shared_ptr`,
`std::weak_ptr`, standard containers, or a third-party runtime.

```cpp
#include <own/ownership.hpp>

struct asset { int version; };
auto scene = own::make_local<asset>(1);
auto component_a = scene;             // non-atomic local count only
auto component_b = scene;             // same local group

auto job_lease = scene.share();        // explicit global strong reference
// Transfer job_lease through a synchronized queue to a worker.
// On that worker:
auto job_local = std::move(job_lease).localize();
auto sub_operation = job_local;        // non-atomic, on this worker only
```

## Model

- `local_owner<T>`: copies share a non-atomic local group. Each group owns exactly
  one global strong reference, regardless of its local alias count
- `shared_owner<T>`: each handle owns one atomic global strong reference; suitable
  for a synchronized job handoff, a cross-thread cache, or a queued callback
- `weak_owner<T>`: non-owning observation of the global lifetime; `lock()` returns
  a `shared_owner<T>` or an empty handle. Zero strong references is permanent

`share()` is explicit and adds a global reference; it does not consume its source,
including when called on an rvalue. `localize()` explicitly allocates a fresh local
group. An lvalue source keeps its reference; an rvalue source transfers its reference
and becomes empty. Allocation failure leaves the source unchanged.

There is no hidden thread-local registry and no automatic deduplication. Localize
once per resource per scene/worker scope, then copy that local owner. Calling
`localize()` per draw or per component pays repeated allocations and can erase
the benefit. See [design](docs/design.md), the [complete working example](examples/asset_workflow.cpp),
and the [measured benchmark report](docs/benchmark_results.md).

## Thread contract

All operations on a nonempty local owner, including move, read, reset, swap, and
**destruction**, must happen on the thread that created its group. Moving a local
owner into a task does not make it transferable. Cancellation and callback cleanup
also count: a callback holding a local owner cannot be destroyed on another thread.
Capture a shared owner across every boundary and localize only after arrival.

Different shared or weak handle objects may be used concurrently. Concurrent
mutation of the *same* handle needs external synchronization. Publishing a handle
through a queue also requires synchronization. These handles protect lifetime,
not mutable payload state; use immutable assets, locks, or your engine's data rules.

Debug builds check thread confinement with unique thread IDs and abort on a
violation. `OWN_DEBUG_THREAD_CHECK` defaults to 1 without `NDEBUG`, otherwise 0.
Define it identically in every translation unit. Checks are diagnostics, not
synchronization, and do not turn an already-racing program into defined behavior.

## API

```cpp
own::make_local<T>(args...);
own::make_shared<T>(args...);
own::allocate_local<T>(allocator, args...);
own::allocate_shared<T>(allocator, args...);

local.get(); local.reset(); local.swap(other); local.share();
local.use_count();        // global strong refs: groups + shared handles
local.local_use_count();  // aliases in this group only
shared.get(); shared.reset(); shared.swap(other); shared.localize();
shared.localize(group_allocator);
shared.use_count();

own::weak_owner<const T> weak(shared);  // also accepts local owner
weak.lock(); weak.expired(); weak.use_count(); weak.reset(); weak.swap(other);
```

Shared/local handles support `*`, `->`, explicit `bool`, default/`nullptr`
construction, copy/move/assignment, and implicit safe pointer conversions within
the same owner family (for example mutable-to-const or derived-to-base).
There is no public owning raw-pointer constructor. Factories preserve the concrete
object's destructor even when a handle converts to a non-virtual base type.

Counts are instantaneous diagnostics, not synchronization or a reliable uniqueness
test. Global count overflow, local count overflow, invalid allocator callbacks,
and debug thread violations fail fast rather than wrap into lifetime corruption.

## Allocation

`make_shared` uses one allocation for control block and payload. `make_local` uses
that allocation plus one local-group allocation. Every nonempty `localize` costs
one local-group allocation; copying local/shared/weak handles allocates nothing.
The control allocation remains until both the payload is destroyed and all weak
owners are gone, so long-lived weak caches retain that allocation's storage.

An `allocator_ref` is a small non-owning byte-allocation interface:

```cpp
own::allocator_ref allocator{
    &my_pool,
    [](void* ctx, std::size_t bytes, std::size_t alignment) -> void* {
        return static_cast<pool*>(ctx)->allocate(bytes, alignment);
    },
    [](void* ctx, void* ptr, std::size_t bytes, std::size_t alignment) noexcept {
        static_cast<pool*>(ctx)->deallocate(ptr, bytes, alignment);
    }
};
auto asset = own::allocate_local<mesh>(allocator, mesh_description);
```

Return aligned storage or throw; a null result becomes `std::bad_alloc`. Deallocate
must not throw and receives the original size/alignment. The context must remain
valid until the final weak reference and retirement task are released. Shared
control blocks can be freed on any releasing thread: a thread-affine allocator
needs its own safe dispatching layer. Both allocations of `allocate_local` use the
supplied allocator. `shared.localize()` uses the default group allocator unless
one is supplied explicitly. Over-aligned payloads are supported.

## Deferred retirement

Use a retirement hook when last CPU ownership is earlier than safe payload
teardown, for example while a GPU fence is still pending:

```cpp
own::retirement_hook hook{
    &retirement_queue,
    [](void* ctx, own::retirement_task task) noexcept {
        static_cast<queue*>(ctx)->enqueue(std::move(task));
    }
};
auto texture = own::make_shared_with<texture_resource>(hook, description);
// Also: make_local_with, allocate_shared_with, allocate_local_with.
```

The hook receives one move-only task when the final global strong reference drops.
Weak owners expire immediately and cannot resurrect the payload while the task is
queued. `task.run()` destroys the payload exactly once; destruction or replacement
of an unconsumed task also runs it. The task may be moved between threads through a
synchronized queue. The payload's destructor runs on the thread that consumes or
drops that task, so a required device/render thread must be enforced by your queue.

The callback is `noexcept`, must not lose the task, and must have a defined queue-
full policy. Its context must remain alive until the callback finishes. Retaining
all tasks forever retains all payloads. A task is not a GPU fence: the engine must
supply the correct submission/fence association and only run it after completion.
On failed `allocate_local_with` construction, partial work is cleaned immediately;
the retirement hook is installed only after both allocations succeed.

## Build and verify

Only an existing C++20 compiler with exception handling and a POSIX shell are needed
for the supplied scripts:

```sh
./scripts/test.sh
./scripts/benchmark.sh
make example
```

Verified here with GCC 14.2 on Linux x86-64: debug/release, UBSan, TSan, and
ASan+UBSan passed. LeakSanitizer could not run under this environment’s ptrace
setup; ASan was rerun with leak detection disabled. Windows/MSVC, Clang, macOS,
and other CPU architectures are not yet validated. See the [test record](tests/results/README.md)
for exact commands, coverage, logs, and limits.

The test script has sanitizer modes; consult its usage. Tests and examples may use
standard containers and smart pointers as test/reference infrastructure. Those
are not production header dependencies. See `docs/benchmark_results.md` for actual
machine/compiler settings, raw data, limitations, and measured tradeoffs.

## Measured tradeoffs

GCC 14.2, one Linux x86-64 cloud VM, release build, 101 warmed samples per case
and a separate process repeat. These are median CPU-simulation measurements:

| Workload | own | std::shared_ptr |
| --- | ---: | ---: |
| Copy/read/drop from a reused local group | 1.220 ns | 11.897 ns |
| New group plus just one copy | 19.275 ns | 19.248 ns |
| Local asset creation/destruction | 22.219 ns | 11.657 ns |
| Deduplicated frame, 4,096 draws / 64 leases | 3.942 µs | 3.823 µs |
| Full simulated asset lifecycle | 23.949 µs | 35.143 µs |

Local reuse helped ownership-heavy work, but the optimized frame showed no win.
For the benchmark’s 88-byte payload, own requested 160 bytes for its control block
and payload, plus 40 bytes per local group; the standard shared allocation
requested 104 bytes. Group creation and memory overhead are real tradeoffs.

The [full report](docs/benchmark_results.md) includes setup amortization, global
shared/weak costs, contention, async jobs, p95/p99 batch-normalized tails,
allocation counts, source hashes and every raw sample. These results do not
establish a target-engine speedup, individual-operation latency bounds or GPU
performance. Prefer borrowing when a surrounding lease already guarantees life.

## Deliberate boundaries

This is a focused ownership vocabulary, not full `std::shared_ptr` parity. It omits
arrays, aliasing constructors, `void` owners, `enable_shared_from_this`, atomic
owner handles, owner-ordering/hashing, raw-pointer adoption, arbitrary custom
payload deleters, and an STL allocator-traits adapter. Payload cleanup belongs in
a `noexcept` destructor; retirement controls *when/where* that destructor runs.
Strong-reference cycles still leak; use weak links to break cycles.

No claim of measured engine speedup or actual GPU execution is made. Included
engine-shaped workloads are CPU simulations to evaluate ownership and lifetime
choices before integrating with a real engine.

## License

MIT; the original [LICENSE](LICENSE) is preserved.

# ownership_cpp

Header-only C++20 ownership and borrowing in namespace `own`.

Keep ownership at lifetime boundaries. Pass cheap, non-owning views through
ordinary function calls and loops:

- `unique_owner<T>` is the default for exclusive ownership: move it, do not copy it
- `shared_owner<T>` supports multiple independent owners; use it when sharing is needed
- `local_view<T>` borrows access; pointer-sized, trivially copyable, with no allocation,
  reference-count update, or owned guard
- `weak_owner<T>` observes a potentially expired lifetime; `lock()` before use

The production header implements its own reference counting. It uses low-level
standard facilities, not standard smart pointers, containers, or a third-party
runtime. The optional `local_owner<T>` serves a different need: independent owning
copies confined to one thread. It is not a borrowed view.

## Quick start

```cpp
#include <own/ownership.hpp>

struct asset { int version; };
void draw(own::local_view<const asset> view)
{
    // Read view->version. The caller keeps an owner alive for this call.
}

auto owner = own::make_unique<asset>(1);
auto view = owner.borrow();
auto another_view = view;             // copies one pointer; owns nothing
draw(another_view);

// A separately shared lifetime, when multiple owners or weak observation are needed:
auto shared = own::make_shared<asset>(2);
own::weak_owner<asset> observed(shared);
if (auto locked = observed.lock())
{
    draw(locked.borrow());            // locked owns this entire use
}
```

A view does not detect expiration and does not prolong lifetime. Keep an owner
alive for every access and any derived reference. Do not retain the view in a
long-lived object, callback or async job unless that lifetime is covered separately.
Async work can take a moved `unique_owner` when it becomes the sole owner, or a
`shared_owner` when other owners must remain. Borrow only inside the retained scope.
See the [complete asset workflow](examples/asset_workflow.cpp).

## A scope can own once and borrow many times

If the original owner stays alive, no extra guard is needed. For an already shared
object whose source handle might reset, keep one shared copy for the whole scope:

```cpp
auto scope_owner = shared;            // one strong-reference increment
shared.reset();                       // safe: scope_owner still owns the asset
auto view = scope_owner.borrow();
for (int i = 0; i < 100; ++i) { draw(view); }
```

A unique owner cannot be copied to pin an independent scope: keep it alive or move
it into that scope. Unique-to-shared promotion is not provided; choose shared
ownership when constructing an object that needs multiple owners.

There is no privileged root owner and no per-view guard. Any remaining strong
owner keeps the object alive. Views never turn themselves back into ownership.
A `shared_owner` protects lifetime, not unsynchronized mutable payload access.

`borrow()` and `unsafe_get()` reject temporary/rvalue owners to catch obvious
escaping-lifetime mistakes. Ordinary `locked->member` and `weak.lock()->member`
syntax remains available; check a lock succeeded before dereferencing it. C++ can
still retain references or explicitly call `operator->()`, so this is not a borrow
checker or a security boundary. Never use a dangling view, even to adjust a virtual
base pointer during conversion.

## Exclusive ownership

```cpp
auto object = own::make_unique<asset>(3);
auto next_owner = std::move(object);  // object becomes empty; payload stays put
auto view = next_owner.borrow();
next_owner.reset();                  // destroys payload; do not use view afterward
```

The default `unique_owner<T>` contains only `T*`: **8 bytes on the tested 64-bit
ABI**, matching default `std::unique_ptr<T>`. It has no reference count, shared
control block, allocator context or runtime deleter. `make_unique<T>(args...)`
uses ordinary `new T(args...)`; destruction uses typed `delete`. Class-specific
allocation/deallocation functions and constructor-failure cleanup follow C++
new/delete rules together, rather than mixing allocation schemes.

Moves, `reset`, `swap`, `bool`, `*`, `->`, `borrow` and the warned `unsafe_get` are
supported. Safe const qualification is allowed. An owning derived-to-base move
requires public, unambiguous conversion and an accessible, `noexcept`, virtual
base destructor. Non-virtual owning upcasts are rejected: retain the derived
owner and convert its borrowed view to access that base instead. The payload type
must be complete wherever default destruction/reset is instantiated, as with
ordinary standard unique ownership.

### Runtime allocator is explicit opt-in

`allocate_unique<T>(allocator_ref, args...)` returns a different type,
`allocated_unique_owner<T>`. It allocates the object through the byte allocator,
constructs with global placement new, then uses the original concrete destructor
and allocator callback. It does not invoke class-specific storage allocation or
deallocation. Allocation failure/null and constructor unwinding follow the
`allocator_ref` contract.

That opt-in owner stores the access pointer, original allocation address,
allocator context, deallocation function and concrete destruction function:
**40 bytes on this ABI**, without a second heap allocation or reference count.
Its preserved concrete cleanup also allows safe non-virtual-base ownership
conversion. This extra capability and cost do not burden ordinary `make_unique`.
The allocator context is borrowed and must remain valid through disposal; its
`noexcept` deallocation callback must work on the eventual destruction thread.

Neither owner supports copying, `release`, raw/reference adoption, raw reset,
arbitrary user deleters, or conversion to the other owner kind/shared/weak
ownership. Neither provides a retirement hook. They may move between synchronized
threads, but concurrent mutation of the same handle is unsafe. Both retain the
same pointer-sized non-owning `local_view` access model.

[Exclusive-owner measurements](docs/unique_results.md) compare the lean default,
its rejected 40-byte prototype, both standard baselines, and the opt-in type under
matched lifetime contracts. Results distinguish forced-observable moves from
optimized container/frame/job use; the former are not whole-engine timings.
The final 23,808-sample study did **not** establish the requested ~1% goal:
the default pointer-sized owner was close to standard ownership in the tested
workloads, while opt-in allocated-owner move/vector ratios remained 1.548×/1.140×
versus state-matched standard ownership. Confidence intervals and A/A controls
are retained in the report.

## Ownership from `this`

Publicly inherit from `enable_owner_from_this<T>`:

```cpp
struct node : own::enable_owner_from_this<node>
{
    auto retain_for_job() { return shared_from_this(); }
    auto inspect_locally() { return local_from_this(); } // non-owning local_view
};
auto node_owner = own::make_shared<node>();
auto retained = node_owner->shared_from_this();
auto borrowed = node_owner->borrow_from_this();
```

- `shared_from_this()` returns ownership of the **same** control block
- `weak_from_this()` returns a weak observer
- `borrow_from_this()` and `local_from_this()` are synonyms returning non-owning
  views; the caller must already ensure lifetime throughout their use. Unlike
  `owner.borrow()`, these methods make one relaxed atomic count load to check
  registration/liveness; they neither increment the count nor acquire ownership
- Const overloads return `const T` access. Public, unambiguous derived/base and
  virtual inheritance are supported; inaccessible, ambiguous or mismatched bases
  are rejected when a factory instantiates registration
- Shared/local factories register after the object's constructor finishes. Unmanaged objects
  and constructor calls return empty handles/views. After last strong release,
  shared/view results are empty and weak results are expired observers that may
  still retain the control block. A destructor cannot resurrect the object
- Copy/move construction does not copy registration; assignment keeps the target's
  existing registration. A factory-created copy receives its own control block

Publish a factory-created object to other threads only after the factory returns;
registration is initialization, not a concurrent binding protocol.
Call these methods only on an object whose lifetime is already valid. Returning
empty for a live unmanaged/retiring object does not make a dangling `this` safe.
A view extracted from a temporary successful weak lock does not keep that lock's
ownership after the full expression ends; hold the locked owner throughout use.

Unique factories do not register this mixin or create a shared control block.
Under either `unique_owner` or `allocated_unique_owner`, its shared/weak/from-this
view methods remain empty; borrow from the owning handle for access. There is no `unique_from_this()` because it
would create a second exclusive owner, and no automatic unique-to-shared promotion.

No constructor adopts raw `this`, a raw pointer, or an object reference.
See [the working example](examples/owner_from_this.cpp) and [lifetime details](docs/design.md).

## Explicit raw-pointer escape

`get()` has been replaced by `unsafe_get()`. By default its use warns:

```text
Borrowed raw pointer: caller must preserve lifetime; do not delete or otherwise deallocate the returned pointer
```

The returned pointer is borrowed. Never delete, free, adopt, or deallocate it.
There is no implicit raw-pointer conversion or public raw/reference ownership
constructor, reset overload, or adoption API. Prefer `borrow()`, `T&`, or `T const&`.
The same escape warning applies to a view's `unsafe_get()`.

Define `OWN_ENABLE_UNSAFE_GET_WARNING=0` before inclusion to opt out of the warning
(for example for a deliberately audited C API boundary). The default is `1`;
`-Werror=deprecated-declarations` turns this warning into an error. Use one value
consistently across every translation unit. Ordinary `*`, `->`, `bool` and `borrow()`
do not trigger it.

## Optional independent thread-local ownership

Use `local_owner<T>` only when each local copy needs its own independent lifetime,
and all operations, including move and destruction, stay on its group's originating
thread. A group owns one global strong reference; its owning aliases use a
non-atomic local count. `share()` creates a global owner; `shared.localize()` creates
a fresh local group. Cancellation and migrating callbacks must not carry local
owners across threads. Share across the boundary, then localize if needed.

`local` does not mean an automatically enforced lexical lifetime. `local_owner`
has actual thread confinement; `local_view` merely borrows a pointer and contains
no thread ID, owner, or automatic validity check. Borrowed payload access still
needs appropriate lifetime and synchronization.

The shared/local allocation design is unchanged: `make_shared` coallocates object and
control block once; `make_local` additionally allocates a local group, for two
allocations. Every nonempty `localize()` allocates another group. There is no
embedded-group/coallocation optimization in this revision.

## Allocation and deferred retirement

Factories: `make_unique`, `allocate_unique`, `make_shared`, `make_local`,
`allocate_shared`, `allocate_local`. Default `make_unique` uses typed new/delete;
`allocate_unique` returns the separate allocator-aware owner.
An `allocator_ref` supplies non-owning byte-allocation callbacks with exact
size/alignment matching. Null allocations throw `std::bad_alloc`; over-aligned
payloads and constructor failure are supported. Keep allocator contexts alive
until all corresponding control blocks, weak owners and deferred tasks are gone.

The shared/local `_with` factory variants accept a `retirement_hook`. Last strong release makes
weak observers expire immediately, then a move-only `retirement_task` can postpone
payload destruction. The engine must associate and wait for the actual GPU fence;
the library does not do GPU synchronization. Queues must define shutdown and
capacity behavior. See [allocation and retirement contracts](docs/design.md).

## Build, test and reproduce

```sh
./scripts/test.sh
./scripts/benchmark.sh
make example
# This target runs the current debug/release tests before comparing unique owners:
make benchmark-unique
```

Requires an existing C++20 compiler with exceptions and a POSIX shell for the
supplied runners. The library itself is a single header under `include/own`.

The current [unique validation record](tests/results/unique/README.md) includes
unique cases and reruns of the full regression suite. The earlier
[view/from-this record](tests/results/README.md) remains historical evidence.
Validation records contain exact coverage, commands,
source hashes and sanitizer limits. [Benchmark results](docs/benchmark_results.md)
compare equivalent retained-owner borrowing against std ownership plus raw/reference
borrows, and separately measure independent owning copies. Historical data and
fresh reruns of the previous revision are preserved. A borrowed view is not a
faster substitute for an independent lifetime guarantee.

Windows/MSVC, Clang, macOS and other architectures are not yet validated. These
are CPU simulations, not measured engine integration or actual GPU execution.

## Measured borrowing costs

The following published results describe the view/from-this revision; exclusive
ownership additions are measured separately in [unique results](docs/unique_results.md).


GCC 14.2, one Linux x86-64 cloud VM, optimized builds, 101 warmed samples and
an independent process repeat. Both sides retain an owner for the same scope:

| Workload | own view / owner | std owner + raw borrow |
| --- | ---: | ---: |
| Borrowed by-value parameter | 1.357 ns | 1.357 ns |
| 4,096 borrowed draw parameters | 2.882 µs | 2.927 µs |
| One scope pin plus 1,024 reads | 565.887 ns | 405.753 ns |

The repeated draw result was 2.918 vs 2.915 µs: no useful measured draw-loop
advantage. The scope-pin case was slower for own. The benefit is explicit borrowed
access without per-view ownership costs, not a universally faster raw pointer.

The [full report](docs/benchmark_results.md) separates equivalent borrowed
contracts from independent ownership, includes unfavorable results and
batch-normalized p95/p99, and preserves both fresh old-version reruns and the
historical report. Expanding the harness changed some generated code, so old/new
ownership comparisons also use a byte-exact historical-harness control.

## Deliberate boundaries

This is not a replacement for every standard smart-pointer feature. Arbitrary
payload deleters, owning alias constructors and allocator traits are deferred
until an actual integration requires them. Use a resource wrapper with a
`noexcept` destructor for cleanup; `allocator_ref` controls storage separately.
If a dependency needs the full standard pointer interface, using that interface
there can be simpler than expanding this library preemptively.

There is no full `std::unique_ptr` / `std::shared_ptr` parity. Arrays, `void` owners, aliasing ownership,
atomic handle objects, owner-ordering/hashing, raw adoption, arbitrary payload
deleters and an STL allocator-traits adapter are omitted. Payload destructors must
be `noexcept`. Strong-reference cycles still need weak links.

MIT; the original [LICENSE](LICENSE) is preserved.

# Ownership and lifetime design

## Primary vocabulary: own the scope, borrow the access

Prefer `unique_owner<T>` when one movable owner can cover the required lifetime.
It creates no reference count or shared control block. Use `shared_owner<T>` when
multiple independent owners or weak observations are actually needed.

`shared_owner<T>` provides an independent lifetime guarantee. Use it for stored
ownership, async handoff, and a scope whose source owner might reset. There is no
privileged original/root owner: every remaining strong reference has the same
lifetime authority.

`borrowed<T>` is a pointer-sized, trivially copyable borrowed view. It contains
only `T*`: no control-block pointer, count, allocation, weak observation, thread ID,
or guard ownership. `owner.borrow()` and view copies do not update reference
counts. A view does not expire automatically; its non-null state is not proof that
an object remains alive. Its lifetime is the caller's responsibility.

`borrowed<T>` was called `local_view<T>`. That name remains an exact alias, so
existing code and overloads are unaffected, but "local" now names only the
thread confinement of `local_owner`; a view has no thread affinity at all.

If an owner already covers a synchronous function/loop, simply borrow from it.
If that owner might reset, copy one shared owner into a scope pin and borrow from
the pin. Copying each view does not copy the pin. A callback/job whose lifetime is
independent captures a shared owner, then borrows during execution.

A const `shared_owner<T>&` behaves like a const pointer handle, not a pointer to a
const payload: its borrowed type remains `borrowed<T>`. Use `shared_owner<const T>`
or convert a view to `borrowed<const T>` when immutable access is required.

There is deliberately no automatic view-to-owner promotion. Views and owners have
different contracts, so their timings must be compared with equivalent contracts:
retained shared ownership plus raw/reference borrows for views, and independent
owning handles for ownership-copy measurements.

Named raw extraction uses `unsafe_get()`, with the configured deprecation warning.
The previous `get()` API is removed. Owners' `borrow()` and `unsafe_get()` are
lvalue-only; their const-rvalue overloads are deleted. This prevents a common
immediately-dangling expression, not every possible reference escape. `*` and `->`
remain usable for normal full-expression access. Always check weak lock success.
C++ permits retaining returned references and explicit operator calls, so it does
not offer a complete lifetime checker here. Even converting a borrowed view to a
virtual base needs the pointed-to object alive during pointer adjustment.

### Static lifetime analysis

Under Clang the owners are annotated `[[gsl::Owner]]`, the view
`[[gsl::Pointer]]`, and `borrow()` `[[clang::lifetimebound]]`. Clang's built-in
analysis then reports, at no runtime cost, a view that provably outlives the
owner it came from:

```cpp
own::borrowed<int> leak() {
    auto owner = own::make_shared<int>(1);
    return owner.borrow(); // -Wreturn-stack-address: 'owner' dies here
}
```

The analysis is local and conservative: it catches returns and initializations
from owners whose end of life is visible in the same function, not views stored
in long-lived objects or captured by deferred work. It is a safety net for
common mistakes, not a borrow checker. Other compilers ignore the annotations;
the tests in `tests/compile_fail/lifetime_*.cpp` run only under Clang.

A view itself has no thread-affinity check. Moving/copying it does not touch a
local ownership group. Any cross-thread borrowed use requires an externally
retained lifetime and correct publication/payload synchronization. Do not infer
thread safety merely from its pointer-sized representation.

## Pointer-only default exclusive ownership

`unique_owner<T>` stores one `T*`, with typed deletion known at compile time.
On the tested 64-bit ABI it is 8 bytes, the same as default standard unique
ownership. There is no reference count, shared control block, erased callback,
allocator context, or metadata allocation in this default type.

`make_unique<T>` uses the ordinary `new T(args...)` expression. `delete T*` follows
the matching language lookup rules, including class-specific allocation,
deallocation, alignment and constructor-failure cleanup. The library does not
allocate with a global byte callback and then accidentally delete through a
class-specific function. The class's allocation functions must themselves obey
C++ requirements. A class-specific nonthrowing allocation function that returns
null yields an empty owner according to new-expression rules; byte-allocator
null results in the separate allocated factory still throw `std::bad_alloc`.

Moves transfer the pointer and empty the source. `reset()` exchanges it for null
before invoking deletion, so reentrant reset observes empty ownership and can
install a replacement. Move assignment first takes the source into a temporary,
then swaps before disposing of the old target; this handles self-move and a source
owner embedded in the old target's payload. Destruction directly deletes the
stored pointer without clearing a handle whose own lifetime is ending. It does
not promise that a callback observes an empty owner during owner destruction;
that guarantee belongs to explicit `reset()`.

The default may add safe const/volatile qualification without changing the delete
type. An owning base conversion is allowed only when pointer conversion is public
and unambiguous and the target has an accessible, `noexcept`, virtual destructor.
Otherwise it would delete through a type that loses the original destruction
contract. A non-virtual-base conversion must use a borrowed view while the derived
owner remains alive. This rejects an unsafe standard-pointer usage rather than
paying type-erasure costs in every default owner.

The type must be complete when deletion/reset is instantiated. Forward declarations
and declarations of owner-returning functions remain possible; an enclosing PIMPL
class should define its destructor where its payload is complete. The default
does not retain a destruction callback merely to permit incomplete-type cleanup.

## Allocators as a type argument

`unique_owner<T, Alloc>` names its allocation in the type. The default,
`own::new_delete`, is the pointer-only owner above. Any other allocator is
passed as the first constructor argument (`unique_owner<T, Alloc>(alloc, args...)`)
or to `allocate_unique<T>(alloc, args...)`; empty allocators also accept
`std::in_place`. Constructors always take `std::in_place` or an allocator first,
so no argument list can be mistaken for adopting an existing object.

### User allocator types

A type with `void* allocate(size_t size, size_t alignment)` and a `noexcept`
`deallocate(void*, size_t size, size_t alignment)`, whose copies and moves are
`noexcept`, satisfies `own::unique_allocator`. The owner stores the allocator
object with `[[no_unique_address]]` (`[[msvc::no_unique_address]]` on MSVC)
before the pointer, so an empty allocator keeps the owner at one pointer and a
handle-like allocator adds only its own size. Moves carry the allocator with the
object; swap exchanges both; move assignment releases the old object to the old
allocator before adopting the new object and its allocator.

The object is one allocation of exactly `sizeof(T)`/`alignof(T)`, constructed with
global placement new. Release destroys it and passes the same size and alignment
back. Because only that size is known, the owner converts only to a
differently cv-qualified `T`: through a base, the allocator would receive the
base's size for a derived object. A null `allocate` result throws
`std::bad_alloc`; a throwing constructor returns its storage before rethrowing.
Owners with different allocator types are distinct types and do not convert.

Choosing a type argument rather than a runtime field keeps the default at one
pointer and lets the compiler inline a pool's calls, at the price that the
allocator becomes part of every signature that names the owner. Code that must
accept owners from several pools uses `allocator_ref`, below, or takes a
`borrowed<T>` when it needs access rather than ownership.

### Runtime allocator: `unique_owner<T, allocator_ref>`

`allocate_unique<T>(allocator_ref, args...)`, or the constructor with an
`allocator_ref`, returns `unique_owner<T, allocator_ref>` (formerly
`allocated_unique_owner<T>`, still available as a deprecated alias). It retains
the original concrete type's disposal callback, original allocation address,
adjusted access pointer, allocator context and deallocator. These five
data/function-pointer fields occupy 40 bytes on the tested ABI. The object itself
is one allocation of exactly `sizeof(T)`/`alignof(T)` through the allocator; no
metadata/control block is separately allocated and no reference count exists.

This path constructs through `::new (storage) T(...)`, bypassing class-specific
storage allocation. It explicitly destroys the concrete payload and calls the
same allocator's deallocator, never a class-specific `operator delete`. Therefore
its erased state can safely retain most-derived destruction after public,
unambiguous non-virtual, multiple or virtual base conversion. Constructor failure
returns storage to the allocator; null allocation throws `std::bad_alloc`.

The callback/context are non-owning and must remain valid until disposal on the
eventual releasing thread. The access pointer alone indicates ownership; cleanup
metadata is meaningful only while that pointer is non-null. Moves null the source
access pointer. Empty move, swap and reset paths never read inactive metadata,
which may outlive its original allocation or allocator context. Reset snapshots
cleanup, makes ownership observably empty before invoking user code, then performs
exactly one destruction/deallocation without touching the handle afterward. Empty handles can be used with incomplete payload types because populated
cleanup was established by a factory where the concrete type was complete.

Owners with different allocator arguments do not implicitly convert to each
other. This prevents silently losing a deallocation contract or putting allocator
state back into the pointer-sized default. Use borrowed access when a consumer only needs
access rather than owning a particular allocation policy.

No form exposes copying, `release`, raw/reference adoption, pointer reset,
public aliasing, arbitrary deleters, arrays, fancy pointers, allocator traits,
retirement hooks or automatic shared promotion. All may move between synchronized
threads; mutation of one handle concurrently remains invalid. A view survives an
ownership move only if the receiving owner continues to retain the same payload.
Reset, replacement and final destruction can invalidate it.

The `enable_owner_from_this` mixin stays unbound under every unique owner;
even its from-this view helpers return empty. Borrow from the owning handle.
Creating a second unique owner from `this` would violate exclusivity.

## Optional two-level strong ownership

A coallocated control block starts with a two-word header: one atomic word
holding the strong and weak counts and a pointer to a static per-type operations
table (dispose, destroy, and where the retirement hook lives). `make_shared` and
`make_local` use a compact block, header plus payload, with the default allocator
and no hook: 24 bytes for an 8-byte payload, the same as libstdc++, against 80
bytes before these layout changes. Factories that
take an allocator or a hook (`allocate_*`, `*_with`) use an extended block that
also stores the allocator context, its deallocation callback and the hook (56
bytes for an 8-byte payload). The allocate callback is not stored: nothing calls
it after the block exists. Every shared owner contributes one strong reference. A
local group contributes one strong reference and holds a thread-confined,
non-atomic alias count; it stores only the deallocation context and callback,
so a group is 40 bytes.

A small header puts the payload on the same cache line as the counts for most
allocation addresses, as in the standard library. Threads that read a payload
while other threads copy or drop owners of the same object then pay for the line
moving. A payload type declared `alignas(64)` (or the platform's destructive
interference size) starts on its own line in either block. See
[the layout measurements](benchmark_layout.md).

A handle stores its adjusted `T*` separately from its ownership record. Base/const
conversion preserves ownership of the original concrete allocation. Multiple
local groups may exist on the same thread or different threads: locality is an
explicit scope, not an implicit map keyed by thread identity.

Both counts live in one 64-bit word: strong in the low 32 bits, weak in the high
32 bits. Increments by a holder of an existing reference use one relaxed
`fetch_add` on that word and check the previous value of their half afterward:
zero (a use after release) or a value at or above `saturation_limit` (2^31)
aborts. The other 2^31 values of each half are headroom for racing increments
that have not yet observed the threshold, so a half can neither wrap through
zero and resurrect a retired block nor carry into the other half. The standard
library also limits counts to 32 bits. Weak locking uses a compare/exchange loop
on the word because it must not increment a zero strong count; it aborts at the
same threshold.

Earlier revisions used a compare/exchange loop for every increment. Under
contention that loop needs extra transfers of the control-block line: on a
4-core Xeon VM, four threads copying one owner measured 367.7/321.1 ns per
copy/drop pair (primary/repeat) with the loop and 206.4/192.9 ns after this change,
below `std::shared_ptr`'s 373.4/342.5 ns in the same runs. Uncontended copies and
weak locking were unchanged. See
[the scaling report](benchmark_scaling.md) for the full method and data.

A strong decrement is a `release` `fetch_sub`. Only the thread that observes the
last strong reference performs an acquire load before disposal, so other
decrements do not pay for acquire ordering (on Arm, `ldaddl` instead of
`ldaddal`; on x86 both are one `lock xadd`). An acquire load rather than a fence
keeps ThreadSanitizer able to model the ordering. If no weak observer existed at
the last strong release, none can be created afterward, because creating one
needs a strong or weak reference; the weak release then sees exactly `weak_one`
and frees the block without a second read-modify-write. Weak releases otherwise
use acquire/release. Weak locking uses a CAS with acquire on success and
succeeds only while the strong count is nonzero. Reads of zero never change it.
Relaxed count queries are advisory snapshots only. No reference-count operation
substitutes for publishing a handle safely or for synchronizing accesses to the
payload.

Loading the word before every decrement to skip both read-modify-writes when the
object was never shared, as libstdc++ does, makes creation and destruction as
cheap as the standard library but was measured to cost 24% on uncontended copies
and 31-40% on contended ones, because copies' releases pay for it too. The check
is therefore confined to the one handle that can plausibly be the sole reference:
a factory sets a hint in the low bit of the handle's block address, moves carry
it and copies clear it. Only a hinted handle loads the word on release; when it
reads one strong reference and only the implicit weak, no other owner, observer
or registration exists and none can appear, so it retires the count with a plain
store. Otherwise, and for every unhinted handle, the release is one `fetch_sub`.
The hint selects a path and never decides: a hinted handle whose object was
copied or observed still decrements, so a stale hint costs one load. The hinted
path is out of line and the common decrement is laid out as the likely branch.
See [the count-layout report](benchmark_counts.md).

## Weak and deferred lifetime

The weak count includes an implicit reference while the object is live, or while
its destruction is represented by a retirement task. Transitioning strong 1→0
transfers this implicit reference to that task. Without a hook its destructor runs
immediately; with a hook it may be queued. Running the task:

1. Clears the task's own pointer to make repeated `run()` harmless
2. Destroys the concrete payload
3. Drops the implicit weak reference
4. Frees the allocation only if no external weak references remain

The implicit weak protects the block even when the payload destroys an embedded
weak handle referring to itself. Hooks may drop external weak references or move
the task to another synchronized context without prematurely freeing the block.

Converting a weak handle between pointer types can require virtual-base pointer
adjustment, which may read the object's vtable. The conversion temporarily locks
the strong lifetime before adjusting; an expired conversion preserves its weak
ownership record but stores no adjusted pointer. This avoids accessing an object
after its lifetime ended. It also means a converting weak operation can briefly
extend lifetime, and its temporary strong release can trigger retirement.

## Local lifetime and failure handling

A local-group allocation is completed before a `shared.localize()` reference
increment or transfer. If allocation throws, the shared source remains unchanged.
Local factories allocate the control block, payload and first group together, so
the only possible allocation failure happens before anything is constructed; no
hook runs for an unsuccessful factory result. A throwing
payload constructor returns its raw block storage to its original allocator.
No-throw destructors are required by factory static assertions.

Local alias count increments/decrements are ordinary accesses. Debug checks use
monotonic per-thread IDs, so a new thread that reuses an old thread's TLS address
will still be rejected. This costs one process-wide atomic ID reservation per
thread in debug builds; ordinary local copies perform no global atomic operation.
`OWN_DEBUG_THREAD_CHECK` defaults from `NDEBUG`, which commonly differs between
a release-built library and a debug application. The check therefore changes
behavior only, never layout: every group stores a thread ID field, and a group
created without checks stores zero and is never checked. Mixing configurations is
memory-safe (a debug reader cannot read past a release-sized group, and every
group is freed with the size it was allocated with), but where configurations mix,
the linker may select either inline body, so diagnostics are only guaranteed when
the whole program uses checks. The field costs 8 bytes per group in every build.
There is no guarantee across unloadable/reloadable dynamic-library boundaries.

### Thread identity across modules

The default thread ID comes from an inline function's static counter and a
`thread_local`. Each Windows DLL, and each ELF shared object built with hidden
visibility, has its own copy of both. The same thread then receives a different
ID in each module, and a `local_owner` created in one module and copied in
another on the *same* thread aborts as if it had crossed threads. The test
`tests/module_boundary` reproduces this as its control case.

Defining `OWN_THREAD_ID_PROVIDER` routes every module to one exported function:

```cpp
// A wrapper header that every module includes instead of the library header:
#define OWN_THREAD_ID_PROVIDER engine_own_thread_id
#ifdef ENGINE_CORE_BUILD
#define OWN_THREAD_ID_PROVIDER_API __declspec(dllexport)
#else
#define OWN_THREAD_ID_PROVIDER_API __declspec(dllimport)
#endif
#include <own/ownership.hpp>

// Exactly one source file of the module that exports it:
OWN_DEFINE_THREAD_ID_PROVIDER()
```

The defined provider is the same monotonic counter, now shared, so IDs still
are never reused by a later thread. An engine with its own thread registry can
define the function itself; it must be `noexcept`, callable on every thread,
and return a nonzero value that no other thread ever receives. Do not use the
operating system's thread ID: it is reused after a thread exits, which would
hide destruction on a new thread after the original one ended. The provider is
called only by checked `local_owner` operations, so release builds without
checks never call it.

### Reporting contract violations

Every violation the library detects (a reference added after release, count
overflow, thread misuse, double registration, an incomplete `allocator_ref`)
calls `OWN_FAIL_FAST(reason)` with an `own::failure` value, then `std::abort()`.
The default hook does nothing, so behavior is unchanged. An engine can route the
reason to its crash reporter:

```cpp
#define OWN_FAIL_FAST(reason) ::engine::fatal_error(::own::to_string(reason))
#include <own/ownership.hpp>
```

The hook may log, flush and terminate, but must not return normally or throw;
abort follows regardless. It is a macro rather than a registered function
pointer because a registered pointer would itself be a per-module global.

### Configuration consistency

`OWN_ENABLE_UNSAFE_GET_WARNING`, `OWN_THREAD_ID_PROVIDER` and `OWN_FAIL_FAST`
must be the same in every translation unit. On MSVC and clang-cl the header
emits `#pragma detect_mismatch` records for them, so a disagreement within one
linked binary is a link error (LNK2038) rather than a silent ODR violation.
`OWN_DEBUG_THREAD_CHECK` is deliberately excluded: it changes behavior only,
never layout, and mixed builds are supported as described above. The record
cannot see across separately linked DLLs; keep settings in one wrapper header.

## Ownership from this

`enable_owner_from_this<T>` contains one weak registration and an empty marker
base. It never owns the object. Shared/local factories finish construction before binding this
registration to their existing control block and the appropriately adjusted `T*`.
No raw `this` adoption or second control block is possible through the public API.
Binding is initialization, not a concurrent registration protocol: do not publish
`this` to another thread from its constructor and race a from-this call with factory
binding. Publish only after the factory returns, through normal synchronization.

The marker detects inheritance even when it is private or ambiguous; hidden-friend
lookup then requires exactly one accessible, unambiguous enabling base. The type
named by the mixin must be an accessible, unambiguous base of the factory type.
Public inherited `enable_owner_from_this<Base>` through a derived object and
virtual inheritance are supported. Unsupported/private/ambiguous/mismatched
registration is a compile-time factory error.

Methods return mutable or const results matching the receiver:

- `shared_from_this()` locks the registered weak reference
- `weak_from_this()` copies a weak observer
- `borrow_from_this()` returns a non-owning view when the registration currently
  has a nonzero strong count, otherwise an empty view. `local_from_this()` is a
  deprecated synonym kept for compatibility

The last operation performs one relaxed atomic strong-count load through the
registered weak reference. It does not increment a count or pin the object;
ordinary `owner.borrow()` and view copies do not even read a count. Its caller must
already guarantee the object's lifetime; it is not a replacement for `weak.lock()`.
Calling any member through a dangling `this` is already invalid; empty-result
handling applies only when the object itself is still alive. A temporary locked
owner cannot cover borrowed use beyond that temporary's lifetime.
Both view methods reject rvalue object receivers. Unmanaged objects and constructor
calls are unregistered and yield empty results. A weak handle captured before
binding stays empty afterward. In normal/deferred destruction the strong count is
already zero, so methods cannot resurrect the object or manufacture valid access.
At that point shared owners/views returned are empty, while `weak_from_this()` can
still return an expired observer that retains control storage.

Copy/move constructors leave the new mixin unregistered. Copy/move assignment does
not change the target registration. A factory-created copy/move registers its new
object with its own block; the source's registration remains with the source.
Const factories retain internal registration but expose const access via their
const receiver methods. Owned mixin objects must not replace their own lifetime
with placement construction while handles exist, just as ordinary owned objects
must not do so.

Registration adds one external weak reference. During final payload destruction,
the mixin's weak member is destroyed before the implicit weak reference is dropped;
the block therefore cannot disappear underneath the destructor. Deferred tasks
retain the implicit weak reference until payload destruction completes. Local
factories have no allocation after payload construction and registration.

## Allocation and retirement contracts

`make_unique` uses typed new/delete with only a pointer in its handle.
`allocate_unique` uses a byte allocator and an explicitly stateful allocated owner.
`make_shared` uses one coallocated payload/control allocation. `make_local` and
`allocate_local(_with)` reserve their first local group inside that allocation:
the group holds a strong reference, so its storage never outlives the block, and
releasing it only ends the group object. Every further nonempty `localize()`
creates a new group, even when another group already exists on the same thread.
With the default allocator, each thread keeps the storage of one freed group and
reuses it for its next `localize()`; groups are released on their creating
thread, so the slot needs no synchronization. A thread-exit cleanup frees the
slot, and groups released later during thread-exit destruction free their own
storage. Custom allocators always receive every group allocation.

A group created by a local factory is *exclusive* while its strong reference is
the only reference of any kind to the block: no `share()`, no weak observer, and
no ownership-from-this registration (checked once, when the group is created).
Every way to create another reference starts from this group on its own thread,
so the group can track exclusivity non-atomically (the top bit of its alias
count, keeping it five words). `share()` and weak observation of a local owner
end it permanently. When an exclusive group's last alias is released, no other
thread can hold or create a reference, so the strong count is retired with a
plain store instead of an atomic decrement; disposal, hooks and the weak release
proceed as usual. A local owner that is created, used and dropped without being
shared therefore performs no atomic read-modify-write.

An `allocator_ref` contains a context and byte allocate/deallocate callbacks.
Return suitably aligned storage or throw; null becomes `std::bad_alloc`.
Deallocate is `noexcept` and receives the exact original size/alignment. Allocator
contexts are non-owning and must outlive their allocations, including weak tails
and retirement tasks. A global control block can be freed on any releasing thread.
`allocate_local` uses its allocator for both allocations; `shared.localize()` uses
the default group allocator unless one is supplied explicitly.

A `retirement_hook` receives one move-only task at final strong release. Its
callback is `noexcept` and must run or retain the task using a defined queue-full
policy. Dropping/replacing a task runs it; running twice is harmless. The task may
cross threads through a synchronized queue, but payload teardown happens on the
thread running/dropping it. The engine must choose and enforce any device-thread
requirement, real fence association, and shutdown drain policy. The callback
context must remain alive through callback completion; deferred task/allocator
storage must remain valid until final release. No actual GPU operation is implied.

## Recommended engine flow

- Asset database/cache: shared owners for owned entries, weak owners for optional
  observation. Removing the cache entry releases only that cache's ownership
- Scene load: keep shared owners for resources whose lifetime the scene controls.
  Pass views/references for ordinary access. If individual stored local copies
  truly need independent lifetimes on a pinned thread, optionally reuse one
  `local_owner` group per resource
- Frame gathering: deduplicate resource identities/version IDs before acquiring
  frame leases; a frame should not copy a global owner once per draw packet
- Async work: enqueue shared leases and borrow while the dequeued owner remains
  alive. Use optional local ownership only if independent local alias lifetimes
  are actually needed; destroy all such owners on their group's original thread
- Migrating tasks and cancellation: keep a shared owner in the queued closure.
  No local owner may be captured in a closure whose destruction thread can change
- Hot reload: publish a new immutable version, retain old-version owners for
  in-flight scenes/jobs/frames, and let old versions retire independently
- GPU retirement: bind each resource's last-use fence to a retirement queue entry.
  Last CPU ownership is not GPU completion. Queue shutdown must drain safely

Local groups are useful when many local aliases amortize a group allocation and
share/localize boundary costs. For a single transient owner, a global handle or a
borrow under a clearly longer-lived lease may be simpler and faster. Beyond the
built-in one-slot cache, resource pooling may help allocation-heavy localization,
but pools must preserve alignment, context lifetime, and cross-thread
deallocation requirements.

## Casts and identity

`static_owner_cast<To>` and `dynamic_owner_cast<To>` mirror the standard pointer
casts for `shared_owner` and `local_owner`. A copy cast adds a reference to the
same control block (or local group); an rvalue cast transfers the source's
reference. A failed dynamic cast returns an empty owner and leaves an rvalue
source untouched, so the caller never loses ownership by asking. Const is never
removed. There is no general aliasing constructor: the result always points at
the same object, adjusted only by a language cast.

Exclusive owners take only `dynamic_owner_cast`, from an rvalue. `unique_owner<T>`
deletes through its own type, so the target must have a virtual destructor (or
be the same type); otherwise the cast does not compile.
`unique_owner<T, allocator_ref>` keeps its concrete disposer and allocation, so
any successful cast destroys correctly. An owner with a user allocator type can
never hold a derived object through a base, so it offers no downcast.

Owners still have no `==`, ordering or hash. `own::identity(owner)` returns an
`own::object_id`: a comparable, hashable key that cannot be dereferenced.
Shared, weak and local handles are keyed by their control block, so every handle
and every cast of one object compare equal, and a weak key does not change when
the object expires. Exclusive owners are keyed by their allocation (for
`unique_owner`, the most-derived object). A key is unique only while its block
or allocation exists; afterwards the address can be reused, exactly as with
`std::owner_less`.

## Validation scope

Testing includes owner/view contract separation, this registration, ordinary and
converting ownership, weak lifetime races, failure
injection, deferred retirement, compile-time interface boundaries, multiple
translation units, and thread misuse diagnostics. Sanitizer support depends on
what the execution environment permits. A passing stress suite is evidence, not
a formal proof of a concurrent algorithm. Real engine validation should include
its actual schedulers, thread migration/cancellation, allocators, shutdown ordering,
resource reload patterns, GPU APIs, fence values, and platform toolchains.

Each compile-fail case states the reason it must be rejected (`// expect:`
patterns matched against the compiler's output), so a case cannot pass by
failing for an unrelated reason. CI builds and runs the suite with GCC, Clang,
MSVC and clang-cl through CMake/CTest; with `cl.exe`, whose wording differs,
only rejection is checked for the standard-diagnostic cases. Layout claims are
`static_assert`s in `tests/layout_tests.cpp` and therefore hold on every
platform that builds the tests.

# Ownership and lifetime design

## Two levels of strong ownership

A coallocated control block holds atomic `strong` and `weak` counters, destruction
function pointers, allocation callbacks, and an optional retirement hook. Every
shared owner contributes one strong reference. A local group contributes one
strong reference and holds a thread-confined, non-atomic alias count.

A handle stores its adjusted `T*` separately from its ownership record. Base/const
conversion preserves ownership of the original concrete allocation. Multiple
local groups may exist on the same thread or different threads: locality is an
explicit scope, not an implicit map keyed by thread identity.

Strong and weak increments use compare/exchange loops with fail-fast overflow
checking before mutation. An unchecked fetch-add can wrap through zero during a
race; this implementation does not permit that transient state. CAS can be more
expensive than `std::shared_ptr`'s common fetch-add implementation under contention.
This is an intentional safety/performance tradeoff, measured separately.

Strong decrements use acquire/release atomics. The thread observing the final
reference sees prior releases and dispatches disposal. Weak locking uses a CAS
with acquire on success and succeeds only while the strong count is nonzero.
Reads of zero never change it. Relaxed count queries are advisory snapshots only.
No reference-count operation substitutes for publishing a handle safely or for
synchronizing accesses to the payload.

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
A failed local factory destroys its already-constructed payload and deallocates
its control block; no hook runs for the unsuccessful factory result. A throwing
payload constructor returns its raw block storage to its original allocator.
No-throw destructors are required by factory static assertions.

Local alias count increments/decrements are ordinary accesses. Debug checks use
monotonic per-thread IDs, so a new thread that reuses an old thread's TLS address
will still be rejected. This costs one process-wide atomic ID reservation per
thread in debug builds; ordinary local copies perform no global atomic operation.
Debug check configuration must be uniform across a program's translation units.
There is no guarantee across unloadable/reloadable dynamic-library boundaries.

## Recommended engine flow

- Asset database/cache: shared owners for owned entries, weak owners for optional
  observation. Removing the cache entry releases only that cache's ownership
- Scene load: localize each resource once on the scene's pinned thread, then local
  copy into component handles. Duplicates reuse the existing scene-local group
- Frame gathering: deduplicate resource identities/version IDs before acquiring
  frame leases; a frame should not copy a global owner once per draw packet
- Async work: enqueue shared leases. Localize after dequeue, copy locally during
  execution, and destroy every local copy on that worker before leaving it
- Migrating tasks and cancellation: keep a shared owner in the queued closure.
  No local owner may be captured in a closure whose destruction thread can change
- Hot reload: publish a new immutable version, retain old-version owners for
  in-flight scenes/jobs/frames, and let old versions retire independently
- GPU retirement: bind each resource's last-use fence to a retirement queue entry.
  Last CPU ownership is not GPU completion. Queue shutdown must drain safely

Local groups are useful when many local aliases amortize a group allocation and
share/localize boundary costs. For a single transient owner, a global handle or a
borrow under a clearly longer-lived lease may be simpler and faster. Resource
pooling may help allocation-heavy localization, but pools must preserve alignment,
context lifetime, and cross-thread deallocation requirements.

## Validation scope

Testing includes ordinary and converting ownership, weak lifetime races, failure
injection, deferred retirement, compile-time interface boundaries, multiple
translation units, and thread misuse diagnostics. Sanitizer support depends on
what the execution environment permits. A passing stress suite is evidence, not
a formal proof of a concurrent algorithm. Real engine validation should include
its actual schedulers, thread migration/cancellation, allocators, shutdown ordering,
resource reload patterns, GPU APIs, fence values, and platform toolchains.

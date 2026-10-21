#include <own/ownership.hpp>

#include "test_support.hpp"

#include <atomic>
#include <cstdlib>
#include <new>
#include <thread>

namespace {
std::atomic<int> fail_after{-1};
std::atomic<int> live_allocations{0};

bool fail_now() noexcept {
    const int previous = fail_after.load(std::memory_order_relaxed);
    if (previous < 0) return false;
    fail_after.store(previous - 1, std::memory_order_relaxed);
    return previous == 0;
}
} // namespace

// Aligned storage for the replaced aligned operator new. MSVC has no
// std::aligned_alloc, and its aligned blocks must be released with _aligned_free.
// Out of line for the same reason as the replacement functions: GCC's
// mismatched-new-delete analysis must not see an inlined free on its own.
#if defined(__GNUC__) || defined(__clang__)
#define OWN_TEST_ALIGNED_NOINLINE __attribute__((noinline))
#else
#define OWN_TEST_ALIGNED_NOINLINE
#endif
#if defined(_WIN32)
#include <malloc.h>
OWN_TEST_ALIGNED_NOINLINE static void* test_aligned_allocate(std::size_t alignment, std::size_t size) {
    return _aligned_malloc(size == 0 ? 1 : size, alignment);
}
OWN_TEST_ALIGNED_NOINLINE static void test_aligned_free(void* pointer) { _aligned_free(pointer); }
#else
OWN_TEST_ALIGNED_NOINLINE static void* test_aligned_allocate(std::size_t alignment, std::size_t size) {
    // aligned_alloc requires size to be an exact multiple of alignment.
    const auto rounded_size = ((size + alignment - 1) / alignment) * alignment;
    return std::aligned_alloc(alignment, rounded_size == 0 ? alignment : rounded_size);
}
OWN_TEST_ALIGNED_NOINLINE static void test_aligned_free(void* pointer) { std::free(pointer); }
#endif

// Dedicated executable: replace global new only here, never in the ordinary
// unit suite. This verifies the actual make_* and default localize paths.
void* operator new(std::size_t size) {
    if (fail_now()) throw std::bad_alloc();
    if (void* result = std::malloc(size == 0 ? 1 : size)) {
        ++live_allocations;
        return result;
    }
    throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept {
    if (pointer) {
        --live_allocations;
        std::free(pointer);
    }
}
void operator delete(void* pointer, std::size_t) noexcept { ::operator delete(pointer); }

void* operator new(std::size_t size, std::align_val_t requested_alignment) {
    if (fail_now()) throw std::bad_alloc();
    const auto alignment = static_cast<std::size_t>(requested_alignment);
    if (void* result = test_aligned_allocate(alignment, size)) {
        ++live_allocations;
        return result;
    }
    throw std::bad_alloc();
}
OWN_TEST_ALIGNED_NOINLINE void operator delete(void* pointer, std::align_val_t) noexcept {
    if (pointer) {
        --live_allocations;
        test_aligned_free(pointer);
    }
}
void operator delete(void* pointer, std::size_t, std::align_val_t alignment) noexcept {
    ::operator delete(pointer, alignment);
}

namespace {
struct tracked {
    int* destroyed;
    explicit tracked(int& count) : destroyed(&count) {}
    ~tracked() { ++*destroyed; }
};
struct alignas(1024) aligned_tracked : tracked { using tracked::tracked; };

template <class F> bool throws_bad_alloc(F&& body) {
    try { body(); }
    catch (const std::bad_alloc&) { return true; }
    return false;
}

template <class T> void factory_failures() {
    const int baseline = live_allocations.load();
    int destroyed = 0;
    fail_after = 0;
    CHECK(throws_bad_alloc([&] { auto owner = own::make_shared<T>(destroyed); (void)owner; }));
    CHECK(destroyed == 0 && live_allocations == baseline);
    fail_after = 0;
    CHECK(throws_bad_alloc([&] { auto owner = own::make_local<T>(destroyed); (void)owner; }));
    CHECK(destroyed == 0 && live_allocations == baseline);
    // make_local places its first group inside the block: one allocation, so
    // a failure armed for a second allocation is never reached.
    fail_after = 1;
    {
        auto owner = own::make_local<T>(destroyed);
        const bool one_allocation = fail_after.load() == 0 && live_allocations == baseline + 1;
        fail_after = -1;
        CHECK(owner && one_allocation && owner.local_use_count() == 1);
    }
    CHECK(destroyed == 1 && live_allocations == baseline);
    fail_after = -1;
}

void localization_failures() {
    const int baseline = live_allocations.load();
    int destroyed = 0;
    auto owner = own::make_shared<tracked>(destroyed);
    const int held_allocations = live_allocations.load();
    fail_after = 0;
    CHECK(throws_bad_alloc([&] { auto local = owner.localize(); (void)local; }));
    CHECK(owner && owner.use_count() == 1 && destroyed == 0);
    CHECK(live_allocations == held_allocations);
    fail_after = 0;
    CHECK(throws_bad_alloc([&] { auto local = std::move(owner).localize(); (void)local; }));
    CHECK(owner && owner.use_count() == 1 && destroyed == 0);
    CHECK(live_allocations == held_allocations);
    owner.reset();
    CHECK(destroyed == 1 && live_allocations == baseline);
    fail_after = -1;
}
// Default-allocator local groups reuse one freed group's storage per thread.
void localize_reuses_thread_cached_group() {
    int destroyed = 0;
    auto owner = own::make_shared<tracked>(destroyed);
    // Read before the worker exists. Reading it while the worker runs raced
    // with the worker's own allocations and failed intermittently under load.
    const int before = live_allocations.load();
    std::thread worker([&] {
        // Constructed before the cache registers its thread-exit cleanup, so it
        // is destroyed after that cleanup: its group must be freed directly
        // rather than refill an ended cache (ASan reports either mistake).
        thread_local own::local_owner<tracked> late = owner.localize();
        CHECK(late.local_use_count() == 1);
        { auto first = owner.localize(); (void)first; } // fills this thread's cache
        const int baseline = live_allocations.load();
        fail_after = 0; // any allocation would now throw
        bool allocated = false;
        try {
            for (int i = 0; i < 1000; ++i) {
                auto local = owner.localize();
                auto copy = local;
                (void)copy;
            }
        } catch (const std::bad_alloc&) { allocated = true; }
        fail_after = -1;
        CHECK(!allocated && live_allocations == baseline);
    });
    worker.join();
    // Exit freed the cached slot, the late owner's group and the thread's own
    // state: everything the worker allocated is gone, nothing leaked.
    CHECK(live_allocations == before && owner.use_count() == 1 && destroyed == 0);
    owner.reset();
    CHECK(destroyed == 1);
}

void borrowed_access_never_allocates() {
    int destroyed = 0;
    {
        auto shared = own::make_shared<tracked>(destroyed);
        auto local = own::make_local<tracked>(destroyed);
        const int baseline = live_allocations.load();
        fail_after = 0;
        auto first = shared.borrow();
        auto second = local.borrow();
        for (int iteration = 0; iteration < 1000; ++iteration) {
            auto shared_copy = first;
            own::local_view<const tracked> local_copy = second;
            CHECK(shared_copy->destroyed == &destroyed);
            CHECK((*local_copy).destroyed == &destroyed);
        }
        const bool no_attempts = fail_after.load() == 0;
        fail_after = -1;
        CHECK(no_attempts && live_allocations == baseline);
        CHECK(shared.use_count() == 1 && local.use_count() == 1 && local.local_use_count() == 1);
        CHECK(destroyed == 0);
    }
    CHECK(destroyed == 2);
}
} // namespace

int main() {
    test::run("default factory allocation failures", factory_failures<tracked>);
    test::run("aligned default factory allocation failures", factory_failures<aligned_tracked>);
    // Runs before any localize() on this thread, so its group cache is empty.
    test::run("default localization allocation failures", localization_failures);
    test::run("localize reuses thread-cached group storage", localize_reuses_thread_cached_group);
    test::run("borrowed access and copies never allocate", borrowed_access_never_allocates);
    return test::finish();
}

#include <own/ownership.hpp>

#include "test_support.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

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
    // aligned_alloc requires size to be an exact multiple of alignment.
    const auto rounded_size = ((size + alignment - 1) / alignment) * alignment;
    if (void* result = std::aligned_alloc(alignment, rounded_size == 0 ? alignment : rounded_size)) {
        ++live_allocations;
        return result;
    }
    throw std::bad_alloc();
}
void operator delete(void* pointer, std::align_val_t) noexcept { ::operator delete(pointer); }
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
    fail_after = 1;
    CHECK(throws_bad_alloc([&] { auto owner = own::make_local<T>(destroyed); (void)owner; }));
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
    test::run("default localization allocation failures", localization_failures);
    test::run("borrowed access and copies never allocate", borrowed_access_never_allocates);
    return test::finish();
}

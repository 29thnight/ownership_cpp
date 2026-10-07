#include <own/ownership.hpp>

#include "test_support.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {
std::atomic<int> fail_after{-1};
std::atomic<int> live_allocations{0};
std::atomic<int> allocation_attempts{0};

bool fail_now() noexcept {
    ++allocation_attempts;
    const int previous = fail_after.load(std::memory_order_relaxed);
    if (previous < 0) return false;
    fail_after.store(previous - 1, std::memory_order_relaxed);
    return previous == 0;
}
} // namespace

// Dedicated executable: replace global new only here, never in the ordinary
// unit suite. This verifies the actual default make_unique allocation path.
#if defined(__GNUC__) || defined(__clang__)
// Keep the replacement allocation boundary visible. Otherwise GCC's -O3
// mismatched-new-delete analysis sees an inlined free but not its matching
// malloc in the replacement new, incorrectly diagnosing the injected pair.
#define OWN_TEST_NOINLINE __attribute__((noinline))
#else
#define OWN_TEST_NOINLINE
#endif
OWN_TEST_NOINLINE
void* operator new(std::size_t size) {
    if (fail_now()) throw std::bad_alloc();
    if (void* result = std::malloc(size == 0 ? 1 : size)) {
        ++live_allocations;
        return result;
    }
    throw std::bad_alloc();
}
OWN_TEST_NOINLINE void operator delete(void* pointer) noexcept {
    if (pointer) {
        --live_allocations;
        std::free(pointer);
    }
}
void operator delete(void* pointer, std::size_t) noexcept { ::operator delete(pointer); }

OWN_TEST_NOINLINE void* operator new(std::size_t size, std::align_val_t requested_alignment) {
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
#undef OWN_TEST_NOINLINE

namespace {
struct tracked {
    int* destroyed;
    explicit tracked(int& count) : destroyed(&count) {}
    ~tracked() { ++*destroyed; }
};
struct alignas(1024) aligned_tracked : tracked { using tracked::tracked; };
struct self_tracked : tracked, own::enable_owner_from_this<self_tracked> {
    using tracked::tracked;
};
struct throwing_payload {
    struct member_guard { int* count; ~member_guard() { ++*count; } } member;
    explicit throwing_payload(int& count) : member{&count} { throw 17; }
};

template<class T> void single_allocation_and_failure() {
    int destroyed = 0;
    const int baseline = live_allocations.load();
    const int attempts = allocation_attempts.load();
    fail_after = 1;
    auto owner = own::make_unique<T>(destroyed);
    const int remaining = fail_after.load();
    fail_after = -1;
    CHECK(remaining == 0 && allocation_attempts == attempts + 1);
    CHECK(live_allocations == baseline + 1 && destroyed == 0);
    owner.reset();
    CHECK(live_allocations == baseline && destroyed == 1);
    bool caught = false;
    fail_after = 0;
    try { auto failed = own::make_unique<T>(destroyed); (void)failed; }
    catch (const std::bad_alloc&) { caught = true; }
    fail_after = -1;
    CHECK(caught && destroyed == 1 && live_allocations == baseline);
}

void empty_and_move_borrow_paths() {
    int destroyed = 0;
    auto first = own::make_unique<tracked>(destroyed);
    const int baseline = live_allocations.load();
    const int attempts = allocation_attempts.load();
    fail_after = 0;
    own::unique_owner<tracked> empty;
    own::unique_owner<tracked> null(nullptr);
    empty.swap(null);
    empty.reset();
    auto second = std::move(first);
    empty = std::move(second);
    auto view = empty.borrow();
    for (int i = 0; i != 1000; ++i) {
        auto copy = view;
        own::local_view<const tracked> constant = copy;
        (void)constant->destroyed;
    }
    const bool never_allocated = fail_after.load() == 0 && allocation_attempts == attempts;
    fail_after = -1;
    CHECK(never_allocated && live_allocations == baseline && destroyed == 0);
    CHECK(!first && !second && !null && empty);
    empty.reset();
    CHECK(destroyed == 1 && live_allocations == baseline - 1);
}

void constructor_failure_releases_allocation() {
    int member_destroyed = 0;
    const int baseline = live_allocations.load();
    const int attempts = allocation_attempts.load();
    bool caught = false;
    fail_after = 1;
    try { auto owner = own::make_unique<throwing_payload>(member_destroyed); (void)owner; }
    catch (int value) { caught = value == 17; }
    const int remaining = fail_after.load();
    fail_after = -1;
    CHECK(caught && member_destroyed == 1 && live_allocations == baseline);
    CHECK(remaining == 0 && allocation_attempts == attempts + 1);
}

void from_this_never_allocates_or_binds() {
    int destroyed = 0;
    const int baseline = live_allocations.load();
    const int attempts = allocation_attempts.load();
    fail_after = 1;
    auto owner = own::make_unique<self_tracked>(destroyed);
    const bool unbound = !owner->shared_from_this() && owner->weak_from_this().expired() &&
                         !owner->borrow_from_this() && !owner->local_from_this();
    const int remaining = fail_after.load();
    fail_after = -1;
    CHECK(unbound && remaining == 0 && allocation_attempts == attempts + 1);
    CHECK(live_allocations == baseline + 1);
    owner.reset();
    CHECK(destroyed == 1 && live_allocations == baseline);
}

// These allocation pairs deliberately bypass the replaceable global new. A
// typed new-expression must select them, and must use the same pair if the
// constructor throws. Payload class allocation is not a user deleter API.
struct class_allocated {
    inline static int allocations = 0, deallocations = 0, constructed = 0, destroyed = 0;
    inline static void* last_pointer = nullptr;
    inline static std::size_t last_size = 0;
    explicit class_allocated(bool fail = false) {
        if (fail) throw 23;
        ++constructed;
    }
    ~class_allocated() { ++destroyed; }
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    static void* operator new(std::size_t size) {
        ++allocations;
        last_size = size;
        if (void* pointer = std::malloc(size)) return last_pointer = pointer;
        throw std::bad_alloc();
    }
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    static void operator delete(void* pointer) noexcept {
        CHECK(pointer == last_pointer);
        ++deallocations;
        std::free(pointer);
    }
};

struct class_sized_delete {
    inline static int allocations = 0, deallocations = 0;
    inline static std::size_t freed_size = 0;
    int payload = 17;
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    static void* operator new(std::size_t size) {
        ++allocations;
        return ::operator new(size);
    }
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    static void operator delete(void* pointer, std::size_t size) noexcept {
        ++deallocations;
        freed_size = size;
        ::operator delete(pointer);
    }
};

struct alignas(512) class_aligned {
    inline static int allocations = 0, deallocations = 0, destroyed = 0;
    inline static std::size_t allocated_alignment = 0, deallocated_alignment = 0;
    inline static void* last_pointer = nullptr;
    explicit class_aligned(bool fail = false) { if (fail) throw 29; }
    ~class_aligned() { ++destroyed; }
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    static void* operator new(std::size_t size, std::align_val_t alignment) {
        ++allocations;
        allocated_alignment = static_cast<std::size_t>(alignment);
        return last_pointer = ::operator new(size, alignment);
    }
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    static void operator delete(void* pointer, std::align_val_t alignment) noexcept {
        CHECK(pointer == last_pointer);
        ++deallocations;
        deallocated_alignment = static_cast<std::size_t>(alignment);
        ::operator delete(pointer, alignment);
    }
};

struct class_nothrow_null {
    inline static int constructors = 0, destructors = 0, deallocations = 0;
    class_nothrow_null() { ++constructors; }
    ~class_nothrow_null() { ++destructors; }
    static void* operator new(std::size_t) noexcept { return nullptr; }
    static void operator delete(void*) noexcept { ++deallocations; }
};
void class_nothrow_null_is_empty() {
    auto owner = own::make_unique<class_nothrow_null>();
    CHECK(!owner && !owner.borrow());
    owner.reset();
    CHECK(class_nothrow_null::constructors == 0 && class_nothrow_null::destructors == 0);
    CHECK(class_nothrow_null::deallocations == 0);
}

void class_specific_allocation_and_deallocation() {
    class_allocated::allocations = class_allocated::deallocations = 0;
    class_allocated::constructed = class_allocated::destroyed = 0;
    const int baseline = live_allocations.load();
    const int attempts = allocation_attempts.load();
    fail_after = 0;
    auto owner = own::make_unique<class_allocated>();
    const bool no_global_allocation = fail_after == 0 && allocation_attempts == attempts;
    fail_after = -1;
    CHECK(no_global_allocation && live_allocations == baseline);
    CHECK(class_allocated::allocations == 1 && class_allocated::constructed == 1);
    CHECK(class_allocated::last_size == sizeof(class_allocated));
    CHECK(class_allocated::last_pointer == &*owner);
    owner.reset();
    CHECK(class_allocated::destroyed == 1 && class_allocated::deallocations == 1);
    // Const qualification must retain the ordinary class allocation/deletion pair.
    auto constant = own::make_unique<const class_allocated>();
    constant.reset();
    CHECK(class_allocated::allocations == 2 && class_allocated::deallocations == 2);
    CHECK(class_allocated::constructed == 2 && class_allocated::destroyed == 2);
}

void class_specific_constructor_failure_pair() {
    const int allocations = class_allocated::allocations;
    const int deallocations = class_allocated::deallocations;
    const int constructed = class_allocated::constructed;
    const int destroyed = class_allocated::destroyed;
    const int attempts = allocation_attempts.load();
    bool caught = false;
    fail_after = 0;
    try { auto owner = own::make_unique<class_allocated>(true); (void)owner; }
    catch (int value) { caught = value == 23; }
    const bool no_global_allocation = fail_after == 0 && allocation_attempts == attempts;
    fail_after = -1;
    CHECK(caught && no_global_allocation);
    CHECK(class_allocated::allocations == allocations + 1 && class_allocated::deallocations == deallocations + 1);
    CHECK(class_allocated::constructed == constructed && class_allocated::destroyed == destroyed);
}

void class_specific_sized_delete() {
    const int baseline = live_allocations.load();
    auto owner = own::make_unique<class_sized_delete>();
    CHECK(owner->payload == 17 && class_sized_delete::allocations == 1);
    owner.reset();
    CHECK(class_sized_delete::deallocations == 1);
    CHECK(class_sized_delete::freed_size == sizeof(class_sized_delete));
    CHECK(live_allocations == baseline);
}

void class_specific_aligned_pair_and_constructor_failure() {
    const int baseline = live_allocations.load();
    auto owner = own::make_unique<class_aligned>();
    CHECK(class_aligned::allocations == 1 && class_aligned::deallocations == 0);
    CHECK(class_aligned::allocated_alignment == alignof(class_aligned));
    owner.reset();
    CHECK(class_aligned::destroyed == 1 && class_aligned::deallocations == 1);
    CHECK(class_aligned::deallocated_alignment == alignof(class_aligned));
    bool caught = false;
    try { auto failed = own::make_unique<class_aligned>(true); (void)failed; }
    catch (int value) { caught = value == 29; }
    CHECK(caught && class_aligned::allocations == 2 && class_aligned::deallocations == 2);
    CHECK(class_aligned::destroyed == 1 && live_allocations == baseline);
    CHECK(class_aligned::allocated_alignment == class_aligned::deallocated_alignment);
}
} // namespace

int main() {
    test::run("unique default factory exactly one allocation and failure", single_allocation_and_failure<tracked>);
    test::run("unique aligned default factory exactly one allocation and failure", single_allocation_and_failure<aligned_tracked>);
    test::run("unique const default factory exactly one allocation and failure", single_allocation_and_failure<const tracked>);
    test::run("unique empty moves and borrows make no allocation attempts", empty_and_move_borrow_paths);
    test::run("unique default constructor failure frees payload allocation", constructor_failure_releases_allocation);
    test::run("unique from-this neither allocates metadata nor binds", from_this_never_allocates_or_binds);
    test::run("unique honors class-specific new and delete for mutable and const payloads", class_specific_allocation_and_deallocation);
    test::run("unique constructor failure matches class-specific allocation pair", class_specific_constructor_failure_pair);
    test::run("unique honors class-specific sized delete", class_specific_sized_delete);
    test::run("unique honors aligned class allocation pair including constructor failure", class_specific_aligned_pair_and_constructor_failure);
    test::run("unique class noexcept new returning null produces empty owner", class_nothrow_null_is_empty);
    return test::finish();
}

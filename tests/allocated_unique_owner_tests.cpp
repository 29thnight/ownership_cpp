#include <own/ownership.hpp>

#include "test_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
struct tracked {
    int* destroyed;
    int value;
    explicit tracked(int& count, int number = 42) : destroyed(&count), value(number) {}
    ~tracked() { ++*destroyed; }
};

// Fixed storage keeps allocator bookkeeping independent of heap allocation.
struct counting_allocator {
    struct record { void* pointer; std::size_t bytes; std::size_t alignment; };
    std::array<record, 16> live{};
    int attempts = 0;
    int allocated = 0;
    int deallocated = 0;
    bool fail = false;
    bool return_null = false;
    bool mismatch = false;
    record last_allocation{};
    record last_deallocation{};
    std::thread::id freeing_thread{};

    static void* allocate(void* context, std::size_t bytes, std::size_t alignment) {
        auto& self = *static_cast<counting_allocator*>(context);
        ++self.attempts;
        if (self.fail) {
            if (self.return_null) return nullptr;
            throw std::bad_alloc();
        }
        void* pointer = alignment > alignof(std::max_align_t)
            ? ::operator new(bytes, std::align_val_t(alignment)) : ::operator new(bytes);
        for (auto& entry : self.live) {
            if (!entry.pointer) {
                entry = {pointer, bytes, alignment};
                self.last_allocation = entry;
                ++self.allocated;
                return pointer;
            }
        }
        std::abort();
    }

    static void deallocate(void* context, void* pointer, std::size_t bytes,
                           std::size_t alignment) noexcept {
        auto& self = *static_cast<counting_allocator*>(context);
        self.last_deallocation = {pointer, bytes, alignment};
        self.freeing_thread = std::this_thread::get_id();
        ++self.deallocated;
        for (auto& entry : self.live) {
            if (entry.pointer == pointer) {
                self.mismatch |= entry.bytes != bytes || entry.alignment != alignment;
                if (entry.alignment > alignof(std::max_align_t))
                    ::operator delete(pointer, std::align_val_t(entry.alignment));
                else
                    ::operator delete(pointer);
                entry = {};
                return;
            }
        }
        self.mismatch = true;
    }

    own::allocator_ref ref() { return {this, &allocate, &deallocate}; }
    void check_balanced() const {
        CHECK(!mismatch && allocated == deallocated);
        for (const auto& entry : live) CHECK(!entry.pointer);
    }
};

struct left_base { int left = 17; };
struct right_base {
    int right = 31;
    int* destroyed;
    explicit right_base(int& count) : destroyed(&count) {}
    ~right_base() { ++*destroyed; } // Deliberately nonvirtual.
};
struct derived : left_base, right_base {
    int* derived_destroyed;
    derived(int& base_count, int& derived_count)
        : right_base(base_count), derived_destroyed(&derived_count) {}
    ~derived() { ++*derived_destroyed; }
};
struct virtual_base {
    int value = 91;
    int* destroyed;
    explicit virtual_base(int& count) : destroyed(&count) {}
    ~virtual_base() { ++*destroyed; } // Virtual inheritance, nonvirtual destructor.
};
struct virtual_left : virtual virtual_base {
    explicit virtual_left(int& count) : virtual_base(count) {}
    int padding = 7;
};
struct virtual_right : virtual virtual_base {
    explicit virtual_right(int& count) : virtual_base(count) {}
    int padding = 19;
};
struct virtual_derived : virtual_left, virtual_right {
    int* destroyed;
    virtual_derived(int& base_count, int& derived_count)
        : virtual_base(base_count), virtual_left(base_count), virtual_right(base_count),
          destroyed(&derived_count) {}
    ~virtual_derived() { ++*destroyed; }
};
struct alignas(512) aligned_tracked : tracked {
    using tracked::tracked;
    std::array<std::byte, 513> payload{};
};
struct throwing_value {
    struct member_guard { int* count; ~member_guard() { ++*count; } } member;
    explicit throwing_value(int& count) : member{&count} {
        throw std::runtime_error("expected constructor failure");
    }
};
struct hidden_new : tracked {
    using tracked::tracked;
    static void* operator new(std::size_t) = delete;
    static void* operator new(std::size_t, void*) = delete;
    static void operator delete(void*) = delete;
};
struct self_aware : own::enable_owner_from_this<self_aware> {
    int* destroyed;
    bool constructor_unbound;
    bool* destructor_unbound;
    explicit self_aware(int& count, bool& destructor_state)
        : destroyed(&count),
          constructor_unbound(!shared_from_this() && weak_from_this().expired() &&
                              !borrow_from_this() && !local_from_this()),
          destructor_unbound(&destructor_state) {}
    ~self_aware() {
        *destructor_unbound = !shared_from_this() && weak_from_this().expired() &&
                              !borrow_from_this() && !local_from_this();
        ++*destroyed;
    }
};

template<class T> concept has_get = requires(T& value) { value.get(); };
template<class T> concept has_release = requires(T& value) { value.release(); };
template<class T> concept has_use_count = requires(T& value) { value.use_count(); };
template<class T> concept has_rvalue_borrow = requires(T&& value) { std::move(value).borrow(); };
template<class T> concept has_raw_reset = requires(T& value, typename T::element_type* p) { value.reset(p); };
static_assert(sizeof(own::allocated_unique_owner<int>) == 5 * sizeof(void*));
static_assert(std::is_same_v<decltype(own::allocate_unique<int>({}, 1)), own::allocated_unique_owner<int>>);
static_assert(!std::is_constructible_v<own::unique_owner<int>, own::allocated_unique_owner<int>>);
static_assert(!std::is_constructible_v<own::allocated_unique_owner<int>, own::unique_owner<int>>);
static_assert(!std::is_copy_constructible_v<own::allocated_unique_owner<int>>);
static_assert(!std::is_copy_assignable_v<own::allocated_unique_owner<int>>);
static_assert(std::is_nothrow_move_constructible_v<own::allocated_unique_owner<int>>);
static_assert(std::is_nothrow_move_assignable_v<own::allocated_unique_owner<int>>);
static_assert(std::is_nothrow_destructible_v<own::allocated_unique_owner<int>>);
static_assert(std::is_nothrow_default_constructible_v<own::allocated_unique_owner<int>>);
static_assert(std::is_nothrow_constructible_v<own::allocated_unique_owner<int>, std::nullptr_t>);
static_assert(!std::is_constructible_v<own::allocated_unique_owner<int>, int*>);
static_assert(!std::is_constructible_v<own::allocated_unique_owner<int>, int&>);
static_assert(!std::is_convertible_v<own::allocated_unique_owner<int>, int*>);
static_assert(!std::is_convertible_v<own::allocated_unique_owner<int>, bool>);
static_assert(std::is_nothrow_constructible_v<own::allocated_unique_owner<right_base>, own::allocated_unique_owner<derived>&&>);
static_assert(std::is_nothrow_assignable_v<own::allocated_unique_owner<right_base>&, own::allocated_unique_owner<derived>&&>);
static_assert(std::is_convertible_v<own::allocated_unique_owner<int>, own::allocated_unique_owner<const int>>);
static_assert(!std::is_constructible_v<own::allocated_unique_owner<int>, own::allocated_unique_owner<const int>&&>);
static_assert(!std::is_constructible_v<own::allocated_unique_owner<derived>, own::allocated_unique_owner<right_base>&&>);
static_assert(!std::is_constructible_v<own::allocated_unique_owner<int>, own::shared_owner<int>>);
static_assert(!std::is_constructible_v<own::shared_owner<int>, own::allocated_unique_owner<int>>);
static_assert(!std::is_constructible_v<own::local_owner<int>, own::allocated_unique_owner<int>>);
static_assert(!std::is_constructible_v<own::weak_owner<int>, own::allocated_unique_owner<int>>);
static_assert(!std::is_constructible_v<own::allocated_unique_owner<int>, own::local_view<int>>);
static_assert(!has_get<own::allocated_unique_owner<int>> && !has_release<own::allocated_unique_owner<int>>);
static_assert(!has_use_count<own::allocated_unique_owner<int>> && !has_raw_reset<own::allocated_unique_owner<int>>);
static_assert(!has_rvalue_borrow<own::allocated_unique_owner<int>>);
static_assert(!has_rvalue_borrow<const own::allocated_unique_owner<int>>);
static_assert(std::is_same_v<own::allocated_unique_owner<int>::element_type, int>);
static_assert(std::is_same_v<decltype(std::declval<const own::allocated_unique_owner<int>&>().borrow()), own::local_view<int>>);
static_assert(std::is_same_v<decltype(std::declval<const own::allocated_unique_owner<const int>&>().borrow()), own::local_view<const int>>);
static_assert(noexcept(std::declval<const own::allocated_unique_owner<int>&>().borrow()));
static_assert(noexcept(std::declval<own::allocated_unique_owner<int>&>().reset()));
static_assert(noexcept(std::declval<own::allocated_unique_owner<int>&>().swap(std::declval<own::allocated_unique_owner<int>&>())));

template<class F> void expect_bad_alloc(F&& body) {
    bool caught = false;
    try { body(); } catch (const std::bad_alloc&) { caught = true; }
    CHECK(caught);
}

void empty_handles() {
    own::allocated_unique_owner<int> first;
    own::allocated_unique_owner<int> second(nullptr);
    CHECK(!first && !second && !first.borrow());
    first.reset();
    first.swap(second);
    auto moved = std::move(first);
    second = std::move(moved);
    CHECK(!first && !second && !moved);
}

void make_and_reset() {
    int destroyed = 0;
    auto owner = own::allocate_unique<tracked>({}, destroyed, 123);
    CHECK(owner && owner->value == 123 && (*owner).value == 123);
    owner.reset();
    CHECK(!owner && destroyed == 1);
    owner.reset();
    CHECK(destroyed == 1);
}

void exact_payload_allocation() {
    counting_allocator allocator;
    int destroyed = 0;
    {
        auto owner = own::allocate_unique<tracked>(allocator.ref(), destroyed, 29);
        CHECK(allocator.attempts == 1 && allocator.allocated == 1 && allocator.deallocated == 0);
        CHECK(allocator.last_allocation.pointer == std::addressof(*owner));
        CHECK(allocator.last_allocation.bytes == sizeof(tracked));
        CHECK(allocator.last_allocation.alignment == alignof(tracked));
        CHECK(owner->value == 29);
    }
    CHECK(destroyed == 1);
    allocator.check_balanced();
}

void move_construction() {
    counting_allocator allocator;
    int destroyed = 0;
    auto first = own::allocate_unique<tracked>(allocator.ref(), destroyed);
    auto* pointer = std::addressof(*first);
    auto second = std::move(first);
    CHECK(!first && second && std::addressof(*second) == pointer);
    auto third = std::move(first);
    CHECK(!third && destroyed == 0 && allocator.attempts == 1);
    second.reset();
    CHECK(destroyed == 1);
    allocator.check_balanced();
}

void move_assignment() {
    counting_allocator first_allocator, second_allocator;
    int first_destroyed = 0, second_destroyed = 0;
    auto first = own::allocate_unique<tracked>(first_allocator.ref(), first_destroyed, 1);
    auto second = own::allocate_unique<tracked>(second_allocator.ref(), second_destroyed, 2);
    auto* second_pointer = std::addressof(*second);
    first = std::move(second);
    CHECK(!second && first->value == 2 && std::addressof(*first) == second_pointer);
    CHECK(first_destroyed == 1 && second_destroyed == 0);
    first_allocator.check_balanced();
    own::allocated_unique_owner<tracked> empty;
    first = std::move(empty);
    CHECK(!first && !empty && second_destroyed == 1);
    second_allocator.check_balanced();
}

void self_move_and_swap() {
    int destroyed = 0;
    auto owner = own::allocate_unique<tracked>({}, destroyed, 53);
    auto* pointer = std::addressof(*owner);
    auto& alias = owner;
    owner = std::move(alias);
    CHECK(owner && std::addressof(*owner) == pointer && destroyed == 0);
    owner.swap(owner);
    CHECK(owner->value == 53 && destroyed == 0);
    owner.reset();
    owner = std::move(alias);
    CHECK(!owner && destroyed == 1);
}

void swapping_allocator_provenance() {
    counting_allocator first_allocator, second_allocator;
    int first_destroyed = 0, second_destroyed = 0;
    auto first = own::allocate_unique<tracked>(first_allocator.ref(), first_destroyed, 11);
    auto second = own::allocate_unique<tracked>(second_allocator.ref(), second_destroyed, 22);
    first.swap(second);
    CHECK(first->value == 22 && second->value == 11);
    own::allocated_unique_owner<tracked> empty;
    using std::swap;
    swap(first, empty);
    CHECK(!first && empty->value == 22);
    second.reset();
    CHECK(first_destroyed == 1 && second_destroyed == 0);
    first_allocator.check_balanced();
    empty.reset();
    CHECK(second_destroyed == 1);
    second_allocator.check_balanced();
}

void adjusted_base_conversion() {
    counting_allocator allocator;
    int base_destroyed = 0, derived_destroyed = 0;
    auto source = own::allocate_unique<derived>(allocator.ref(), base_destroyed, derived_destroyed);
    auto* original = std::addressof(*source);
    auto* adjusted = static_cast<right_base*>(original);
    CHECK(static_cast<void*>(original) != static_cast<void*>(adjusted));
    own::allocated_unique_owner<right_base> target = std::move(source);
    CHECK(!source && std::addressof(*target) == adjusted && target->right == 31);
    target.reset();
    CHECK(base_destroyed == 1 && derived_destroyed == 1);
    CHECK(allocator.last_deallocation.pointer == original);
    CHECK(allocator.last_deallocation.bytes == sizeof(derived));
    CHECK(allocator.last_deallocation.alignment == alignof(derived));
    allocator.check_balanced();
}

void converting_move_assignment() {
    counting_allocator source_allocator, target_allocator;
    int source_base = 0, source_derived = 0, target_destroyed = 0;
    auto source = own::allocate_unique<derived>(source_allocator.ref(), source_base, source_derived);
    auto target = own::allocate_unique<right_base>(target_allocator.ref(), target_destroyed);
    auto* adjusted = static_cast<right_base*>(std::addressof(*source));
    target = std::move(source);
    CHECK(!source && std::addressof(*target) == adjusted && target_destroyed == 1);
    CHECK(source_base == 0 && source_derived == 0);
    target_allocator.check_balanced();
    own::allocated_unique_owner<derived> empty;
    target = std::move(empty);
    CHECK(!target && source_base == 1 && source_derived == 1);
    source_allocator.check_balanced();
}

void virtual_base_conversion() {
    counting_allocator allocator;
    int base_destroyed = 0, derived_destroyed = 0;
    auto source = own::allocate_unique<virtual_derived>(allocator.ref(), base_destroyed, derived_destroyed);
    auto* original = std::addressof(*source);
    auto* adjusted = static_cast<virtual_base*>(original);
    own::allocated_unique_owner<virtual_left> middle = std::move(source);
    own::allocated_unique_owner<const virtual_base> target = std::move(middle);
    CHECK(!source && !middle && std::addressof(*target) == adjusted && target->value == 91);
    target.reset();
    CHECK(base_destroyed == 1 && derived_destroyed == 1);
    CHECK(allocator.last_deallocation.pointer == original);
    CHECK(allocator.last_deallocation.bytes == sizeof(virtual_derived));
    CHECK(allocator.last_deallocation.alignment == alignof(virtual_derived));
    allocator.check_balanced();
    own::allocated_unique_owner<virtual_derived> empty;
    own::allocated_unique_owner<const virtual_base> empty_base = std::move(empty);
    CHECK(!empty && !empty_base);
}

void const_factories_and_conversion() {
    counting_allocator allocator;
    int destroyed = 0;
    auto source = own::allocate_unique<tracked>(allocator.ref(), destroyed, 81);
    own::allocated_unique_owner<const tracked> target = std::move(source);
    CHECK(!source && target->value == 81);
    auto constant = own::allocate_unique<const tracked>(allocator.ref(), destroyed, 82);
    CHECK(constant->value == 82);
    const auto integer = own::allocate_unique<const int>({}, 83);
    CHECK(*integer == 83);
    target.reset();
    constant.reset();
    CHECK(destroyed == 2);
    allocator.check_balanced();
}

void perfect_forwarding() {
    struct move_only_payload {
        std::unique_ptr<int> value;
        int* reference;
        move_only_payload(std::unique_ptr<int> input, int& ref)
            : value(std::move(input)), reference(&ref) {}
    };
    int reference = 47;
    auto input = std::make_unique<int>(38);
    auto value = own::allocate_unique<move_only_payload>({}, std::move(input), reference);
    CHECK(!input && *value->value == 38 && value->reference == &reference);
    CHECK(*own::allocate_unique<int>({}) == 0);
}

void over_aligned_allocation() {
    counting_allocator allocator;
    int destroyed = 0;
    {
        auto owner = own::allocate_unique<aligned_tracked>(allocator.ref(), destroyed, 73);
        CHECK(reinterpret_cast<std::uintptr_t>(std::addressof(*owner)) % alignof(aligned_tracked) == 0);
        CHECK(allocator.last_allocation.bytes == sizeof(aligned_tracked));
        CHECK(allocator.last_allocation.alignment == alignof(aligned_tracked));
        own::allocated_unique_owner<tracked> base = std::move(owner);
        CHECK(base->value == 73);
    }
    CHECK(destroyed == 1);
    CHECK(allocator.last_deallocation.bytes == sizeof(aligned_tracked));
    CHECK(allocator.last_deallocation.alignment == alignof(aligned_tracked));
    allocator.check_balanced();
    {
        auto owner = own::allocate_unique<aligned_tracked>({}, destroyed);
        CHECK(reinterpret_cast<std::uintptr_t>(std::addressof(*owner)) % alignof(aligned_tracked) == 0);
    }
    CHECK(destroyed == 2);
}

void allocator_failure(bool returns_null) {
    counting_allocator allocator;
    allocator.fail = true;
    allocator.return_null = returns_null;
    int destroyed = 0;
    expect_bad_alloc([&] { auto owner = own::allocate_unique<tracked>(allocator.ref(), destroyed); (void)owner; });
    CHECK(allocator.attempts == 1 && allocator.allocated == 0 && allocator.deallocated == 0);
    CHECK(destroyed == 0);
    allocator.check_balanced();
}

void constructor_failure() {
    counting_allocator allocator;
    int member_destroyed = 0;
    bool caught = false;
    try { auto owner = own::allocate_unique<throwing_value>(allocator.ref(), member_destroyed); (void)owner; }
    catch (const std::runtime_error&) { caught = true; }
    CHECK(caught && member_destroyed == 1 && allocator.attempts == 1);
    CHECK(allocator.last_allocation.pointer == allocator.last_deallocation.pointer);
    CHECK(allocator.last_deallocation.bytes == sizeof(throwing_value));
    CHECK(allocator.last_deallocation.alignment == alignof(throwing_value));
    allocator.check_balanced();
}

void class_specific_new_is_not_used() {
    int destroyed = 0;
    { auto owner = own::allocate_unique<hidden_new>({}, destroyed); CHECK(owner->value == 42); }
    CHECK(destroyed == 1);
}

void borrowed_access_survives_owner_move() {
    counting_allocator allocator;
    int destroyed = 0;
    auto owner = own::allocate_unique<tracked>(allocator.ref(), destroyed, 92);
    const auto& const_handle = owner;
    auto view = const_handle.borrow();
    own::local_view<const tracked> const_view = view;
    for (int i = 0; i != 1000; ++i) {
        auto copy = view;
        CHECK(copy->value == 92 && const_view->value == 92);
    }
    auto retained = std::move(owner);
    CHECK(!owner && view->value == 92 && std::addressof(*view) == std::addressof(*retained));
    CHECK(allocator.attempts == 1 && allocator.deallocated == 0 && destroyed == 0);
    retained.reset();
    // Never inspect or dereference views after the payload has expired.
    CHECK(destroyed == 1);
    allocator.check_balanced();
}

void self_registration_stays_unbound() {
    counting_allocator allocator;
    int destroyed = 0;
    bool destructor_unbound = false;
    auto owner = own::allocate_unique<self_aware>(allocator.ref(), destroyed, destructor_unbound);
    CHECK(owner->constructor_unbound);
    CHECK(!owner->shared_from_this() && owner->weak_from_this().expired());
    CHECK(!owner->borrow_from_this() && !owner->local_from_this());
    const auto& payload = *owner;
    CHECK(!payload.shared_from_this() && payload.weak_from_this().expired());
    CHECK(!payload.borrow_from_this() && !payload.local_from_this());
    CHECK(owner.borrow()->constructor_unbound && allocator.attempts == 1);
    auto moved = std::move(owner);
    CHECK(!moved->shared_from_this());
    moved.reset();
    CHECK(destroyed == 1 && destructor_unbound);
    allocator.check_balanced();
    auto constant = own::allocate_unique<const self_aware>({}, destroyed, destructor_unbound);
    CHECK(!constant->shared_from_this() && !constant->borrow_from_this());
    constant.reset();
    CHECK(destroyed == 2 && destructor_unbound);
}

void reset_is_empty_during_destruction() {
    struct reentrant {
        own::allocated_unique_owner<reentrant>* owner;
        bool* saw_empty;
        int* destroyed;
        reentrant(own::allocated_unique_owner<reentrant>& slot, bool& empty, int& count)
            : owner(&slot), saw_empty(&empty), destroyed(&count) {}
        ~reentrant() {
            *saw_empty = !*owner;
            owner->reset();
            ++*destroyed;
        }
    };
    own::allocated_unique_owner<reentrant> owner;
    bool saw_empty = false;
    int destroyed = 0;
    owner = own::allocate_unique<reentrant>({}, owner, saw_empty, destroyed);
    owner.reset();
    CHECK(saw_empty && destroyed == 1 && !owner);
}

void move_from_member_of_replaced_payload() {
    struct node {
        own::allocated_unique_owner<node> next;
        int* destroyed;
        int value;
        node(int& count, int number) : destroyed(&count), value(number) {}
        ~node() { ++*destroyed; }
    };
    counting_allocator allocator;
    int destroyed = 0;
    auto owner = own::allocate_unique<node>(allocator.ref(), destroyed, 1);
    owner->next = own::allocate_unique<node>(allocator.ref(), destroyed, 2);
    auto* replacement = std::addressof(*owner->next);
    // The source handle lives inside the payload destroyed by this assignment.
    owner = std::move(owner->next);
    CHECK(destroyed == 1 && owner->value == 2 && std::addressof(*owner) == replacement);
    CHECK(allocator.allocated == 2 && allocator.deallocated == 1);
    owner = std::move(owner->next);
    CHECK(!owner && destroyed == 2);
    allocator.check_balanced();
}

void reset_destructor_installs_replacement() {
    struct reentrant {
        own::allocated_unique_owner<reentrant>* target;
        own::allocated_unique_owner<reentrant>* replacement;
        int* destroyed;
        bool* saw_empty;
        reentrant(own::allocated_unique_owner<reentrant>* slot, own::allocated_unique_owner<reentrant>* incoming,
                  int& count, bool& empty)
            : target(slot), replacement(incoming), destroyed(&count), saw_empty(&empty) {}
        ~reentrant() {
            if (target) {
                *saw_empty = !*target;
                *target = std::move(*replacement);
            }
            ++*destroyed;
        }
    };
    counting_allocator allocator;
    own::allocated_unique_owner<reentrant> owner, replacement;
    int destroyed = 0;
    bool saw_empty = false;
    replacement = own::allocate_unique<reentrant>(allocator.ref(), nullptr, nullptr, destroyed, saw_empty);
    auto* replacement_pointer = std::addressof(*replacement);
    owner = own::allocate_unique<reentrant>(allocator.ref(), &owner, &replacement, destroyed, saw_empty);
    owner.reset();
    CHECK(saw_empty && destroyed == 1 && !replacement);
    CHECK(owner && std::addressof(*owner) == replacement_pointer);
    CHECK(allocator.allocated == 2 && allocator.deallocated == 1);
    owner.reset();
    CHECK(!owner && destroyed == 2);
    allocator.check_balanced();
}

void synchronized_thread_transfer() {
    counting_allocator allocator;
    int destroyed = 0;
    auto owner = own::allocate_unique<tracked>(allocator.ref(), destroyed, 10);
    // Thread start/join synchronize all handle and payload accesses.
    std::thread worker([incoming = std::move(owner), &owner]() mutable {
        incoming->value += 5;
        owner = std::move(incoming);
    });
    worker.join();
    CHECK(owner && owner->value == 15 && destroyed == 0);
    std::thread::id worker_id;
    std::thread destroyer([incoming = std::move(owner), &worker_id]() mutable {
        worker_id = std::this_thread::get_id();
        incoming.reset();
    });
    destroyer.join();
    CHECK(!owner && destroyed == 1 && allocator.freeing_thread == worker_id);
    allocator.check_balanced();
}

// The handle survives both its old payload allocation and the heap storage
// containing its non-owning allocator context. Tests inspect only the public
// empty state. Runtime tools on this ABI cannot prove that inactive invalid
// pointer values are never copied; that additionally requires invariant review.
void leave_empty_after_context_dies(own::allocated_unique_owner<tracked>& empty,
                                   int& destroyed, bool move_first) {
    auto context = std::make_unique<counting_allocator>();
    empty = own::allocate_unique<tracked>(context->ref(), destroyed, 61);
    if (move_first) {
        auto transferred = std::move(empty);
        CHECK(!empty && transferred->value == 61);
        transferred.reset();
    } else {
        empty.reset();
    }
    CHECK(!empty && !empty.borrow());
    context->check_balanced();
    context.reset();
}

void expired_context_empty_operations(bool move_first) {
    int destroyed = 0;
    own::allocated_unique_owner<tracked> stale;
    leave_empty_after_context_dies(stale, destroyed, move_first);
    CHECK(destroyed == 1 && !stale && !stale.borrow());
    stale.reset();
    stale.reset();
    auto moved = std::move(stale);
    CHECK(!stale && !moved && !moved.borrow());
    stale.swap(moved);
    moved.swap(stale);
    auto& alias = stale;
    stale = std::move(alias);
    stale.swap(stale);
    moved = std::move(stale);
    own::allocated_unique_owner<const tracked> constant = std::move(stale);
    constant = std::move(moved);
    CHECK(!stale && !moved && !constant && !constant.borrow());
    constant.reset();
    CHECK(destroyed == 1);
}

void expired_context_empty_and_live_operations() {
    int expired_destroyed = 0, live_destroyed = 0;
    own::allocated_unique_owner<tracked> stale;
    leave_empty_after_context_dies(stale, expired_destroyed, true);
    counting_allocator context;
    auto live = own::allocate_unique<tracked>(context.ref(), live_destroyed, 77);
    auto* pointer = std::addressof(*live);
    stale.swap(live); // Empty destination must receive only live metadata.
    CHECK(stale && !live && std::addressof(*stale) == pointer && stale->value == 77);
    CHECK(expired_destroyed == 1 && live_destroyed == 0 && context.deallocated == 0);
    stale.swap(live); // Exercise the opposite branch with a live receiver.
    CHECK(!stale && live && std::addressof(*live) == pointer);
    live.swap(stale);
    CHECK(!live && stale && std::addressof(*stale) == pointer);
    live = std::move(stale);
    CHECK(live && !stale && std::addressof(*live) == pointer);
    live.reset();
    CHECK(live_destroyed == 1);
    context.check_balanced();

    // Assigning an empty source whose old context has died must still destroy
    // the target's live payload exactly once using the target's own context.
    leave_empty_after_context_dies(stale, expired_destroyed, true);
    live = own::allocate_unique<tracked>(context.ref(), live_destroyed, 78);
    live = std::move(stale);
    CHECK(!live && !stale && expired_destroyed == 2 && live_destroyed == 2);
    context.check_balanced();
    live.reset();
    stale.reset();
}

void expired_context_empty_virtual_base_conversions() {
    own::allocated_unique_owner<virtual_derived> stale;
    int base_destroyed = 0, derived_destroyed = 0;
    {
        auto context = std::make_unique<counting_allocator>();
        stale = own::allocate_unique<virtual_derived>(context->ref(), base_destroyed, derived_destroyed);
        auto live = std::move(stale);
        CHECK(!stale && live->value == 91);
        live.reset();
        context->check_balanced();
        context.reset();
    }
    CHECK(base_destroyed == 1 && derived_destroyed == 1);
    own::allocated_unique_owner<const virtual_base> direct = std::move(stale);
    own::allocated_unique_owner<virtual_left> middle = std::move(stale);
    own::allocated_unique_owner<const virtual_base> chained = std::move(middle);
    CHECK(!stale && !direct && !middle && !chained);
    CHECK(!stale.borrow() && !direct.borrow() && !middle.borrow() && !chained.borrow());
    counting_allocator live_context;
    int target_destroyed = 0;
    auto target = own::allocate_unique<const virtual_base>(live_context.ref(), target_destroyed);
    target = std::move(stale);
    CHECK(!stale && !target && target_destroyed == 1);
    live_context.check_balanced();
    stale.reset();
    direct.reset();
    middle.reset();
    chained.reset();
    CHECK(base_destroyed == 1 && derived_destroyed == 1);
}

void vector_relocation_and_scope_exit() {
    int destroyed = 0;
    {
        std::vector<own::allocated_unique_owner<tracked>> values;
        for (int i = 0; i != 128; ++i) values.push_back(own::allocate_unique<tracked>({}, destroyed, i));
        CHECK(destroyed == 0);
        for (int i = 0; i != 128; ++i) CHECK(values[static_cast<std::size_t>(i)]->value == i);
    }
    CHECK(destroyed == 128);
}
} // namespace

int main() {
    test::run("allocated unique empty and null handles", empty_handles);
    test::run("allocated unique factory and repeated reset", make_and_reset);
    test::run("allocated unique exact payload-only allocation", exact_payload_allocation);
    test::run("allocated unique move construction and empty source", move_construction);
    test::run("allocated unique move assignment releases prior ownership", move_assignment);
    test::run("allocated unique self move and self swap", self_move_and_swap);
    test::run("allocated unique swap preserves allocator provenance", swapping_allocator_provenance);
    test::run("allocated unique adjusted nonvirtual base conversion", adjusted_base_conversion);
    test::run("allocated unique converting move assignment", converting_move_assignment);
    test::run("allocated unique chained virtual-base conversion", virtual_base_conversion);
    test::run("allocated unique const factories and conversion", const_factories_and_conversion);
    test::run("allocated unique perfect forwarding and value initialization", perfect_forwarding);
    test::run("allocated unique over-aligned allocation and base cleanup", over_aligned_allocation);
    test::run("allocated unique throwing allocation failure", [] { allocator_failure(false); });
    test::run("allocated unique null allocation failure", [] { allocator_failure(true); });
    test::run("allocated unique constructor failure frees original storage", constructor_failure);
    test::run("allocated unique bypasses class-specific new and delete", class_specific_new_is_not_used);
    test::run("allocated unique borrowing does not own or allocate", borrowed_access_survives_owner_move);
    test::run("allocated unique from-this registration remains unbound", self_registration_stays_unbound);
    test::run("allocated unique reset is empty during reentrant destruction", reset_is_empty_during_destruction);
    test::run("allocated unique move assignment from nested source payload", move_from_member_of_replaced_payload);
    test::run("allocated unique reset destructor preserves installed replacement", reset_destructor_installs_replacement);
    test::run("allocated unique synchronized cross-thread transfer and cleanup", synchronized_thread_transfer);
    test::run("allocated unique vector relocation destroys exactly once", vector_relocation_and_scope_exit);
    test::run("allocated unique moved-from empty handle outlives payload and heap context", [] { expired_context_empty_operations(true); });
    test::run("allocated unique reset-empty handle outlives payload and heap context", [] { expired_context_empty_operations(false); });
    test::run("allocated unique expired-context empty and live swaps and assignment", expired_context_empty_and_live_operations);
    test::run("allocated unique expired-context empty const and virtual-base conversions", expired_context_empty_virtual_base_conversions);
    return test::finish();
}

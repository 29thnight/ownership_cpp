#include <own/ownership.hpp>

#include "test_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
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
struct nonvirtual_base { int base = 31; };
struct nonvirtual_derived : nonvirtual_base {};
struct private_derived : private nonvirtual_base {};
struct ambiguous_left : nonvirtual_base {};
struct ambiguous_right : nonvirtual_base {};
struct ambiguous_derived : ambiguous_left, ambiguous_right {};
struct protected_virtual_base { protected: virtual ~protected_virtual_base() = default; };
struct public_derived : protected_virtual_base { ~public_derived() override = default; };
struct virtual_base {
    int* destroyed;
    int value = 91;
    explicit virtual_base(int& count) : destroyed(&count) {}
    virtual ~virtual_base() noexcept { ++*destroyed; }
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
    inline static int allocations = 0;
    inline static int deallocations = 0;
    inline static void* allocated_pointer = nullptr;
    inline static void* deallocated_pointer = nullptr;
    int* destroyed;
    virtual_derived(int& base_count, int& derived_count)
        : virtual_base(base_count), virtual_left(base_count), virtual_right(base_count),
          destroyed(&derived_count) {}
    ~virtual_derived() override { ++*destroyed; }
    static void* operator new(std::size_t size) {
        ++allocations;
        return allocated_pointer = ::operator new(size);
    }
    static void operator delete(void* pointer) noexcept {
        ++deallocations;
        deallocated_pointer = pointer;
        ::operator delete(pointer);
    }
};
struct alignas(512) aligned_tracked : tracked {
    using tracked::tracked;
    std::array<std::byte, 513> payload{};
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
static_assert(sizeof(own::unique_owner<int>) == sizeof(int*));
static_assert(alignof(own::unique_owner<int>) == alignof(int*));
static_assert(sizeof(own::unique_owner<virtual_base>) == sizeof(virtual_base*));
static_assert(sizeof(own::unique_owner<aligned_tracked>) == sizeof(aligned_tracked*));
static_assert(std::is_same_v<decltype(own::make_unique<int>(1)), own::unique_owner<int>>);
static_assert(!std::is_copy_constructible_v<own::unique_owner<int>>);
static_assert(!std::is_copy_assignable_v<own::unique_owner<int>>);
static_assert(std::is_nothrow_move_constructible_v<own::unique_owner<int>>);
static_assert(std::is_nothrow_move_assignable_v<own::unique_owner<int>>);
static_assert(std::is_nothrow_destructible_v<own::unique_owner<int>>);
static_assert(std::is_nothrow_default_constructible_v<own::unique_owner<int>>);
static_assert(std::is_nothrow_constructible_v<own::unique_owner<int>, std::nullptr_t>);
static_assert(!std::is_constructible_v<own::unique_owner<int>, int*>);
static_assert(!std::is_constructible_v<own::unique_owner<int>, int&>);
static_assert(!std::is_convertible_v<own::unique_owner<int>, int*>);
static_assert(!std::is_convertible_v<own::unique_owner<int>, bool>);
static_assert(!std::is_constructible_v<own::unique_owner<nonvirtual_base>, own::unique_owner<nonvirtual_derived>&&>);
static_assert(!std::is_assignable_v<own::unique_owner<nonvirtual_base>&, own::unique_owner<nonvirtual_derived>&&>);
static_assert(!std::is_constructible_v<own::unique_owner<protected_virtual_base>, own::unique_owner<public_derived>&&>);
static_assert(!std::is_constructible_v<own::unique_owner<nonvirtual_base>, own::unique_owner<private_derived>&&>);
static_assert(!std::is_constructible_v<own::unique_owner<nonvirtual_base>, own::unique_owner<ambiguous_derived>&&>);
static_assert(std::is_nothrow_constructible_v<own::unique_owner<virtual_base>, own::unique_owner<virtual_derived>&&>);
static_assert(std::is_nothrow_assignable_v<own::unique_owner<virtual_base>&, own::unique_owner<virtual_derived>&&>);
static_assert(std::is_convertible_v<own::unique_owner<int>, own::unique_owner<const int>>);
static_assert(!std::is_constructible_v<own::unique_owner<int>, own::unique_owner<const int>&&>);
static_assert(!std::is_constructible_v<own::unique_owner<virtual_derived>, own::unique_owner<virtual_base>&&>);
static_assert(!std::is_constructible_v<own::unique_owner<int>, own::shared_owner<int>>);
static_assert(!std::is_constructible_v<own::shared_owner<int>, own::unique_owner<int>>);
static_assert(!std::is_constructible_v<own::local_owner<int>, own::unique_owner<int>>);
static_assert(!std::is_constructible_v<own::weak_owner<int>, own::unique_owner<int>>);
static_assert(!std::is_constructible_v<own::unique_owner<int>, own::local_view<int>>);
static_assert(!std::is_constructible_v<own::unique_owner<int>, own::allocated_unique_owner<int>>);
static_assert(!std::is_constructible_v<own::allocated_unique_owner<int>, own::unique_owner<int>>);
static_assert(!has_get<own::unique_owner<int>> && !has_release<own::unique_owner<int>>);
static_assert(!has_use_count<own::unique_owner<int>> && !has_raw_reset<own::unique_owner<int>>);
static_assert(!has_rvalue_borrow<own::unique_owner<int>> && !has_rvalue_borrow<const own::unique_owner<int>>);
static_assert(std::is_same_v<own::unique_owner<int>::element_type, int>);
static_assert(std::is_same_v<decltype(std::declval<const own::unique_owner<int>&>().borrow()), own::local_view<int>>);
static_assert(std::is_same_v<decltype(std::declval<const own::unique_owner<const int>&>().borrow()), own::local_view<const int>>);
static_assert(noexcept(std::declval<const own::unique_owner<int>&>().borrow()));
static_assert(noexcept(std::declval<own::unique_owner<int>&>().reset()));
static_assert(noexcept(std::declval<own::unique_owner<int>&>().swap(std::declval<own::unique_owner<int>&>())));

void empty_handles() {
    own::unique_owner<int> first;
    own::unique_owner<int> second(nullptr);
    CHECK(!first && !second && !first.borrow());
    first.reset();
    first.swap(second);
    auto moved = std::move(first);
    second = std::move(moved);
    CHECK(!first && !second && !moved);
}
void make_and_reset() {
    int destroyed = 0;
    auto owner = own::make_unique<tracked>(destroyed, 123);
    CHECK(owner && owner->value == 123 && (*owner).value == 123);
    owner.reset();
    CHECK(!owner && destroyed == 1);
    owner.reset();
    CHECK(destroyed == 1);
}
void moves_and_swaps() {
    int first_destroyed = 0, second_destroyed = 0;
    auto first = own::make_unique<tracked>(first_destroyed, 1);
    auto second = own::make_unique<tracked>(second_destroyed, 2);
    auto* pointer = std::addressof(*second);
    first = std::move(second);
    CHECK(!second && first->value == 2 && std::addressof(*first) == pointer);
    CHECK(first_destroyed == 1 && second_destroyed == 0);
    auto third = std::move(first);
    auto moved_empty = std::move(first);
    CHECK(!first && !moved_empty && std::addressof(*third) == pointer);
    own::unique_owner<tracked> empty;
    using std::swap;
    swap(third, empty);
    CHECK(!third && std::addressof(*empty) == pointer);
    empty = std::move(moved_empty);
    CHECK(!empty && !moved_empty && second_destroyed == 1);
}
void self_move_and_swap() {
    int destroyed = 0;
    auto owner = own::make_unique<tracked>(destroyed, 53);
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
void nonvirtual_base_borrow() {
    auto owner = own::make_unique<nonvirtual_derived>();
    own::local_view<nonvirtual_base> base = owner.borrow();
    own::local_view<const nonvirtual_base> constant = base;
    CHECK(base->base == 31 && constant->base == 31);
}
void virtual_base_conversion_and_class_delete() {
    virtual_derived::allocations = virtual_derived::deallocations = 0;
    int base_destroyed = 0, derived_destroyed = 0;
    auto source = own::make_unique<virtual_derived>(base_destroyed, derived_destroyed);
    auto* original = std::addressof(*source);
    auto* adjusted = static_cast<virtual_base*>(original);
    CHECK(static_cast<void*>(original) != static_cast<void*>(adjusted));
    own::unique_owner<virtual_left> middle = std::move(source);
    own::unique_owner<const virtual_base> target = std::move(middle);
    CHECK(!source && !middle && std::addressof(*target) == adjusted && target->value == 91);
    target.reset();
    CHECK(base_destroyed == 1 && derived_destroyed == 1);
    CHECK(virtual_derived::allocations == 1 && virtual_derived::deallocations == 1);
    CHECK(virtual_derived::allocated_pointer == original && virtual_derived::deallocated_pointer == original);
    own::unique_owner<virtual_derived> empty;
    own::unique_owner<const virtual_base> empty_base = std::move(empty);
    CHECK(!empty && !empty_base);
}
void converting_move_assignment() {
    int source_base = 0, source_derived = 0, target_destroyed = 0;
    auto source = own::make_unique<virtual_derived>(source_base, source_derived);
    auto target = own::make_unique<virtual_base>(target_destroyed);
    auto* adjusted = static_cast<virtual_base*>(std::addressof(*source));
    target = std::move(source);
    CHECK(!source && std::addressof(*target) == adjusted && target_destroyed == 1);
    CHECK(source_base == 0 && source_derived == 0);
    own::unique_owner<virtual_derived> empty;
    target = std::move(empty);
    CHECK(!target && source_base == 1 && source_derived == 1);
}
void const_factories_and_conversion() {
    int destroyed = 0;
    auto source = own::make_unique<tracked>(destroyed, 81);
    own::unique_owner<const tracked> target = std::move(source);
    CHECK(!source && target->value == 81);
    auto constant = own::make_unique<const tracked>(destroyed, 82);
    const auto integer = own::make_unique<const int>(83);
    CHECK(constant->value == 82 && *integer == 83);
    target.reset();
    constant.reset();
    CHECK(destroyed == 2);
}
void perfect_forwarding_and_alignment() {
    struct move_only_payload {
        std::unique_ptr<int> value;
        int* reference;
        move_only_payload(std::unique_ptr<int> input, int& ref) : value(std::move(input)), reference(&ref) {}
    };
    int reference = 47;
    auto input = std::make_unique<int>(38);
    auto value = own::make_unique<move_only_payload>(std::move(input), reference);
    CHECK(!input && *value->value == 38 && value->reference == &reference);
    CHECK(*own::make_unique<int>() == 0);
    int destroyed = 0;
    {
        auto aligned = own::make_unique<aligned_tracked>(destroyed);
        CHECK(reinterpret_cast<std::uintptr_t>(std::addressof(*aligned)) % alignof(aligned_tracked) == 0);
    }
    CHECK(destroyed == 1);
}
void borrowed_access_survives_owner_move() {
    int destroyed = 0;
    auto owner = own::make_unique<tracked>(destroyed, 92);
    const auto& const_handle = owner;
    auto view = const_handle.borrow();
    own::local_view<const tracked> const_view = view;
    for (int i = 0; i != 1000; ++i) {
        auto copy = view;
        CHECK(copy->value == 92 && const_view->value == 92);
    }
    auto retained = std::move(owner);
    CHECK(!owner && view->value == 92 && std::addressof(*view) == std::addressof(*retained));
    retained.reset();
    // Never inspect or dereference the views after the payload expires.
    CHECK(destroyed == 1);
}
void self_registration_stays_unbound() {
    int destroyed = 0;
    bool destructor_unbound = false;
    auto owner = own::make_unique<self_aware>(destroyed, destructor_unbound);
    CHECK(owner->constructor_unbound);
    CHECK(!owner->shared_from_this() && owner->weak_from_this().expired());
    CHECK(!owner->borrow_from_this() && !owner->local_from_this());
    const auto& payload = *owner;
    CHECK(!payload.shared_from_this() && payload.weak_from_this().expired());
    CHECK(!payload.borrow_from_this() && !payload.local_from_this());
    auto moved = std::move(owner);
    CHECK(!moved->shared_from_this());
    moved.reset();
    CHECK(destroyed == 1 && destructor_unbound);
    auto constant = own::make_unique<const self_aware>(destroyed, destructor_unbound);
    CHECK(!constant->shared_from_this() && !constant->borrow_from_this());
    constant.reset();
    CHECK(destroyed == 2 && destructor_unbound);
}
void reset_is_empty_during_destruction() {
    struct reentrant {
        own::unique_owner<reentrant>* owner;
        bool* saw_empty;
        int* destroyed;
        reentrant(own::unique_owner<reentrant>& slot, bool& empty, int& count)
            : owner(&slot), saw_empty(&empty), destroyed(&count) {}
        ~reentrant() { *saw_empty = !*owner; owner->reset(); ++*destroyed; }
    };
    own::unique_owner<reentrant> owner;
    bool saw_empty = false;
    int destroyed = 0;
    owner = own::make_unique<reentrant>(owner, saw_empty, destroyed);
    owner.reset();
    CHECK(saw_empty && destroyed == 1 && !owner);
}
void move_from_member_of_replaced_payload() {
    struct node {
        own::unique_owner<node> next;
        int* destroyed;
        int value;
        node(int& count, int number) : destroyed(&count), value(number) {}
        ~node() { ++*destroyed; }
    };
    int destroyed = 0;
    auto owner = own::make_unique<node>(destroyed, 1);
    owner->next = own::make_unique<node>(destroyed, 2);
    auto* replacement = std::addressof(*owner->next);
    owner = std::move(owner->next);
    CHECK(destroyed == 1 && owner->value == 2 && std::addressof(*owner) == replacement);
    owner = std::move(owner->next);
    CHECK(!owner && destroyed == 2);
}
void reset_destructor_installs_replacement() {
    struct reentrant {
        own::unique_owner<reentrant>* target;
        own::unique_owner<reentrant>* replacement;
        int* destroyed;
        bool* saw_empty;
        reentrant(own::unique_owner<reentrant>* slot, own::unique_owner<reentrant>* incoming,
                  int& count, bool& empty)
            : target(slot), replacement(incoming), destroyed(&count), saw_empty(&empty) {}
        ~reentrant() {
            if (target) { *saw_empty = !*target; *target = std::move(*replacement); }
            ++*destroyed;
        }
    };
    own::unique_owner<reentrant> owner, replacement;
    int destroyed = 0;
    bool saw_empty = false;
    replacement = own::make_unique<reentrant>(nullptr, nullptr, destroyed, saw_empty);
    auto* replacement_pointer = std::addressof(*replacement);
    owner = own::make_unique<reentrant>(&owner, &replacement, destroyed, saw_empty);
    owner.reset();
    CHECK(saw_empty && destroyed == 1 && !replacement);
    CHECK(owner && std::addressof(*owner) == replacement_pointer);
    owner.reset();
    CHECK(!owner && destroyed == 2);
}
void synchronized_thread_transfer() {
    int destroyed = 0;
    auto owner = own::make_unique<tracked>(destroyed, 10);
    // Thread start/join synchronize all handle and payload accesses.
    std::thread worker([incoming = std::move(owner), &owner]() mutable {
        incoming->value += 5;
        owner = std::move(incoming);
    });
    worker.join();
    CHECK(owner && owner->value == 15 && destroyed == 0);
    std::thread destroyer([incoming = std::move(owner)]() mutable { incoming.reset(); });
    destroyer.join();
    CHECK(!owner && destroyed == 1);
}
void vector_relocation_and_scope_exit() {
    int destroyed = 0;
    {
        std::vector<own::unique_owner<tracked>> values;
        for (int i = 0; i != 128; ++i) values.push_back(own::make_unique<tracked>(destroyed, i));
        CHECK(destroyed == 0);
        for (int i = 0; i != 128; ++i) CHECK(values[static_cast<std::size_t>(i)]->value == i);
    }
    CHECK(destroyed == 128);
}
} // namespace

int main() {
    test::run("lean unique empty and null handles", empty_handles);
    test::run("lean unique factory and repeated reset", make_and_reset);
    test::run("lean unique moves and swaps preserve ownership", moves_and_swaps);
    test::run("lean unique self move and self swap", self_move_and_swap);
    test::run("lean unique nonvirtual base borrowing remains allowed", nonvirtual_base_borrow);
    test::run("lean unique adjusted virtual-base conversion uses dynamic class delete", virtual_base_conversion_and_class_delete);
    test::run("lean unique converting move assignment", converting_move_assignment);
    test::run("lean unique const factories and conversion", const_factories_and_conversion);
    test::run("lean unique perfect forwarding, value initialization, and alignment", perfect_forwarding_and_alignment);
    test::run("lean unique borrowing survives owner move", borrowed_access_survives_owner_move);
    test::run("lean unique from-this registration remains unbound", self_registration_stays_unbound);
    test::run("lean unique reset is empty during reentrant destruction", reset_is_empty_during_destruction);
    test::run("lean unique move assignment from nested source payload", move_from_member_of_replaced_payload);
    test::run("lean unique reset destructor preserves installed replacement", reset_destructor_installs_replacement);
    test::run("lean unique synchronized cross-thread transfer and cleanup", synchronized_thread_transfer);
    test::run("lean unique vector relocation destroys exactly once", vector_relocation_and_scope_exit);
    return test::finish();
}

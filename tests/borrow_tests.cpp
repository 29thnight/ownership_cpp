#include <own/ownership.hpp>

#include "test_support.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace {
struct tracked {
    int* destroyed;
    int value;
    tracked(int& count, int number = 42) : destroyed(&count), value(number) {}
    ~tracked() { ++*destroyed; }
};
struct left_base { int left = 17; };
struct right_base { int right = 31; };
struct derived : left_base, right_base {};
struct virtual_base { int value = 91; virtual ~virtual_base() = default; };
struct virtual_derived : virtual virtual_base { int other = 29; };

struct counting_allocator {
    int allocated = 0;
    int deallocated = 0;
    static void* allocate(void* context, std::size_t bytes, std::size_t alignment) {
        ++static_cast<counting_allocator*>(context)->allocated;
        return ::operator new(bytes, std::align_val_t(alignment));
    }
    static void deallocate(void* context, void* pointer, std::size_t,
                           std::size_t alignment) noexcept {
        ++static_cast<counting_allocator*>(context)->deallocated;
        ::operator delete(pointer, std::align_val_t(alignment));
    }
    own::allocator_ref ref() { return {this, &allocate, &deallocate}; }
};

struct deferred_queue {
    int calls = 0;
    std::optional<own::retirement_task> pending;
    static void retire(void* context, own::retirement_task task) noexcept {
        auto& self = *static_cast<deferred_queue*>(context);
        ++self.calls;
        self.pending.emplace(std::move(task));
    }
    own::retirement_hook hook() { return {this, &retire}; }
};

template<class T> concept has_get = requires(T& object) { object.get(); };
template<class T> concept has_borrow = requires(T& object) { object.borrow(); };
template<class T> concept has_rvalue_borrow = requires(T&& object) { std::move(object).borrow(); };
template<class T> concept has_raw_reset = requires(T& object, int* pointer) { object.reset(pointer); };
template<class T> concept has_share = requires(T& object) { object.share(); };
template<class T> concept has_localize = requires(T& object) { object.localize(); };
template<class T> concept has_lock = requires(T& object) { object.lock(); };
template<class T> concept has_use_count = requires(T& object) { object.use_count(); };

using view = own::local_view<int>;
static_assert(sizeof(view) == sizeof(int*));
static_assert(alignof(view) == alignof(int*));
static_assert(std::is_trivially_copyable_v<view>);
static_assert(std::is_trivially_copy_constructible_v<view>);
static_assert(std::is_trivially_move_constructible_v<view>);
static_assert(std::is_trivially_destructible_v<view>);
static_assert(std::is_nothrow_default_constructible_v<view>);
static_assert(std::is_nothrow_copy_assignable_v<view>);
static_assert(std::is_nothrow_move_assignable_v<view>);
static_assert(!std::is_constructible_v<view, int*>);
static_assert(!std::is_constructible_v<view, int&>);
static_assert(!std::is_constructible_v<view, own::shared_owner<int>>);
static_assert(!std::is_constructible_v<view, own::local_owner<int>>);
static_assert(!std::is_convertible_v<view, int*>);
static_assert(!std::is_convertible_v<own::shared_owner<int>, int*>);
static_assert(!std::is_convertible_v<own::local_owner<int>, int*>);
static_assert(!std::is_constructible_v<own::shared_owner<int>, view>);
static_assert(!std::is_constructible_v<own::local_owner<int>, view>);
static_assert(!std::is_constructible_v<own::weak_owner<int>, view>);
static_assert(!std::is_constructible_v<own::shared_owner<int>, int&>);
static_assert(!std::is_constructible_v<own::local_owner<int>, int&>);
static_assert(!has_get<view> && !has_get<own::shared_owner<int>> && !has_get<own::local_owner<int>>);
static_assert(!has_get<own::weak_owner<int>>);
static_assert(!has_raw_reset<own::shared_owner<int>> && !has_raw_reset<own::local_owner<int>>);
static_assert(!has_share<view> && !has_localize<view> && !has_lock<view> && !has_use_count<view>);
static_assert(has_borrow<own::shared_owner<int>> && has_borrow<const own::shared_owner<int>>);
static_assert(has_borrow<own::local_owner<int>> && has_borrow<const own::local_owner<int>>);
static_assert(!has_borrow<own::weak_owner<int>>);
static_assert(!has_rvalue_borrow<own::shared_owner<int>>);
static_assert(!has_rvalue_borrow<const own::shared_owner<int>>);
static_assert(!has_rvalue_borrow<own::local_owner<int>>);
static_assert(!has_rvalue_borrow<const own::local_owner<int>>);
static_assert(std::is_same_v<decltype(std::declval<const own::shared_owner<int>&>().borrow()), view>);
static_assert(std::is_same_v<decltype(std::declval<const own::local_owner<int>&>().borrow()), view>);
static_assert(std::is_convertible_v<own::local_view<derived>, own::local_view<right_base>>);
static_assert(std::is_convertible_v<own::local_view<virtual_derived>, own::local_view<virtual_base>>);
static_assert(std::is_convertible_v<view, own::local_view<const int>>);
static_assert(!std::is_convertible_v<own::local_view<const int>, view>);
static_assert(!std::is_convertible_v<own::local_view<right_base>, own::local_view<derived>>);
static_assert(std::is_same_v<decltype(*std::declval<const view&>()), int&>);
static_assert(std::is_same_v<decltype(*std::declval<own::local_view<const int>>()), const int&>);

void empty_views() {
    own::local_view<int> empty;
    const auto copy = empty;
    auto moved = std::move(empty);
    CHECK(!empty && !copy && !moved);
    own::shared_owner<int> shared;
    own::local_owner<int> local;
    CHECK(!shared.borrow() && !local.borrow());
    moved = shared.borrow();
    CHECK(!moved);
}

int read_parameter(own::local_view<const tracked> value) { return value->value + (*value).value; }

void retained_shared_owner_access() {
    counting_allocator allocation;
    int destroyed = 0;
    {
        auto owner = own::allocate_shared<tracked>(allocation.ref(), destroyed, 57);
        const auto& constant_owner = owner;
        auto borrowed = constant_owner.borrow();
        CHECK(owner.use_count() == 1 && allocation.allocated == 1);
        {
            std::array<own::local_view<tracked>, 64> copies;
            for (auto& item : copies) item = borrowed;
            auto moved = std::move(borrowed);
            CHECK(std::addressof(*moved) == std::addressof(*owner));
            const auto constant_view = moved;
            constant_view->value = 73;
            for (const auto item : copies) CHECK(read_parameter(item) == 146);
            CHECK(owner.use_count() == 1 && allocation.allocated == 1 && destroyed == 0);
        }
        CHECK(owner.use_count() == 1 && destroyed == 0 && allocation.deallocated == 0);
    }
    CHECK(destroyed == 1 && allocation.allocated == 1 && allocation.deallocated == 1);
}

void retained_local_owner_access() {
    counting_allocator allocation;
    int destroyed = 0;
    {
        auto owner = own::allocate_local<tracked>(allocation.ref(), destroyed);
        auto alias = owner;
        const auto& constant_owner = owner;
        auto borrowed = constant_owner.borrow();
        auto copied = borrowed;
        own::local_view<const tracked> constant = std::move(copied);
        CHECK(read_parameter(constant) == 84);
        CHECK(std::addressof(*borrowed) == std::addressof(*alias));
        CHECK(owner.use_count() == 1 && owner.local_use_count() == 2);
        CHECK(allocation.allocated == 2 && allocation.deallocated == 0 && destroyed == 0);
    }
    CHECK(destroyed == 1 && allocation.allocated == 2 && allocation.deallocated == 2);
}

void views_never_prolong_lifetime() {
    for (bool local : {false, true}) {
        int destroyed = 0;
        own::weak_owner<tracked> weak;
        if (local) {
            auto owner = own::make_local<tracked>(destroyed);
            weak = owner;
            [[maybe_unused]] auto borrowed = owner.borrow();
            owner.reset();
            CHECK(destroyed == 1 && weak.expired());
            // Do not inspect, convert, or dereference the now-dangling view.
        } else {
            auto owner = own::make_shared<tracked>(destroyed);
            weak = owner;
            [[maybe_unused]] auto borrowed = owner.borrow();
            owner.reset();
            CHECK(destroyed == 1 && weak.expired());
        }
        CHECK(destroyed == 1 && !weak.lock());
    }
}

void one_strong_copy_guards_scope() {
    counting_allocator allocation;
    int destroyed = 0;
    auto owner = own::allocate_shared<tracked>(allocation.ref(), destroyed, 19);
    own::weak_owner<tracked> weak = owner;
    {
        auto scope_owner = owner;
        auto borrowed = scope_owner.borrow();
        CHECK(owner.use_count() == 2);
        owner.reset();
        for (int i = 0; i < 1000; ++i) CHECK(read_parameter(borrowed) == 38);
        CHECK(scope_owner.use_count() == 1 && destroyed == 0 && allocation.allocated == 1);
    }
    CHECK(destroyed == 1 && weak.expired() && allocation.deallocated == 0);
    weak.reset();
    CHECK(allocation.deallocated == 1);
}

void safe_view_conversions() {
    auto owner = own::make_shared<derived>();
    auto original = owner.borrow();
    own::local_view<right_base> adjusted = original;
    own::local_view<const right_base> constant = std::move(adjusted);
    own::local_view<right_base> assigned;
    assigned = original;
    CHECK(std::addressof(*constant) == static_cast<right_base*>(std::addressof(*owner)));
    CHECK(std::addressof(*assigned) == std::addressof(*constant));
    CHECK(constant->right == 31 && owner.use_count() == 1);
    auto virtual_owner = own::make_shared<virtual_derived>();
    own::local_view<virtual_base> virtual_view = virtual_owner.borrow();
    own::local_view<const virtual_base> virtual_constant = virtual_view;
    CHECK(std::addressof(*virtual_view) == static_cast<virtual_base*>(std::addressof(*virtual_owner)));
    CHECK(virtual_constant->value == 91 && virtual_owner.use_count() == 1);
    own::local_view<derived> empty;
    own::local_view<right_base> empty_base = empty;
    CHECK(!empty_base);
    auto immutable = own::make_shared<const int>(29);
    CHECK(*immutable.borrow() == 29);
}

void weak_lock_then_borrow() {
    int destroyed = 0;
    auto owner = own::make_shared<tracked>(destroyed, 87);
    own::weak_owner<tracked> weak = owner;
    CHECK(weak.lock()->value == 87 && (*weak.lock()).value == 87);
    auto locked = weak.lock();
    auto borrowed = locked.borrow();
    owner.reset();
    CHECK(borrowed->value == 87 && locked.use_count() == 1 && destroyed == 0);
    locked.reset();
    CHECK(weak.expired() && destroyed == 1);
    // The borrowed view is not accessed after its retained owner is reset.
}

void deferred_retirement_ignores_views() {
    counting_allocator allocation;
    deferred_queue queue;
    int destroyed = 0;
    own::weak_owner<tracked> weak;
    {
        auto owner = own::allocate_shared_with<tracked>(allocation.ref(), queue.hook(), destroyed);
        weak = owner;
        [[maybe_unused]] auto borrowed = owner.borrow();
        owner.reset();
        CHECK(queue.calls == 1 && queue.pending && weak.expired() && destroyed == 0);
    }
    CHECK(queue.calls == 1 && destroyed == 0 && allocation.deallocated == 0);
    queue.pending->run();
    CHECK(destroyed == 1 && allocation.deallocated == 0);
    queue.pending.reset();
    weak.reset();
    CHECK(allocation.allocated == 1 && allocation.deallocated == 1);
}
} // namespace

int main() {
    test::run("empty borrowed views", empty_views);
    test::run("retained shared-owner borrowed access", retained_shared_owner_access);
    test::run("optional retained local-owner borrowed access", retained_local_owner_access);
    test::run("borrowed views never prolong lifetime", views_never_prolong_lifetime);
    test::run("one strong copy guards borrowed scope", one_strong_copy_guards_scope);
    test::run("const and base view conversions", safe_view_conversions);
    test::run("weak lock then borrowed access", weak_lock_then_borrow);
    test::run("deferred retirement ignores borrowed views", deferred_retirement_ignores_views);
    return test::finish();
}

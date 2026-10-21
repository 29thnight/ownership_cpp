// Owner casts, identity keys and the weak converting move.
#include <own/ownership.hpp>

#include "test_support.hpp"

#include <cstddef>
#include <new>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace {
struct base {
    int base_value = 1;
    virtual ~base() noexcept = default;
};
struct other_base {
    int other_value = 2;
    virtual ~other_base() noexcept = default;
};
// other_base sits at a non-zero offset, so casts and identity must adjust.
struct derived : base, other_base {
    int* destroyed;
    explicit derived(int& counter) : destroyed(&counter) {}
    ~derived() override { ++*destroyed; }
};
struct unrelated : base {};

struct counting_allocator {
    int live = 0;
    int deallocations = 0;
    static void* allocate(void* context, std::size_t size, std::size_t alignment) {
        ++static_cast<counting_allocator*>(context)->live;
        return ::operator new(size, std::align_val_t(alignment));
    }
    static void deallocate(void* context, void* pointer, std::size_t, std::size_t alignment) noexcept {
        auto* self = static_cast<counting_allocator*>(context);
        --self->live;
        ++self->deallocations;
        ::operator delete(pointer, std::align_val_t(alignment));
    }
    own::allocator_ref ref() { return {this, &allocate, &deallocate}; }
};

// Exclusive owners take checked downcasts from rvalues only.
template<class To, class Owner>
concept dynamic_castable = requires(Owner&& owner) { own::dynamic_owner_cast<To>(std::forward<Owner>(owner)); };
static_assert(dynamic_castable<derived, own::unique_owner<base>>);
static_assert(!dynamic_castable<derived, own::unique_owner<base>&>);
static_assert(dynamic_castable<derived, own::allocated_unique_owner<base>>);
static_assert(!dynamic_castable<derived, own::allocated_unique_owner<base>&>);
// Const is never removed.
template<class To, class Owner>
concept static_castable = requires(Owner&& owner) { own::static_owner_cast<To>(std::forward<Owner>(owner)); };
static_assert(static_castable<const derived, own::shared_owner<const base>>);
static_assert(!static_castable<derived, own::shared_owner<const base>>);
static_assert(!static_castable<derived, own::local_owner<const base>>);

void shared_static_and_dynamic_casts() {
    int destroyed = 0;
    {
        own::shared_owner<base> as_base = own::make_shared<derived>(destroyed);
        auto as_derived = own::static_owner_cast<derived>(as_base);
        CHECK(as_derived && as_base.use_count() == 2);
        CHECK(as_derived->base_value == 1 && as_derived->other_value == 2);

        own::shared_owner<other_base> as_other = as_derived;
        auto back = own::dynamic_owner_cast<derived>(as_other); // adjusts a non-zero offset
        CHECK(back && back.use_count() == 4 && back->destroyed == &destroyed);

        auto wrong = own::dynamic_owner_cast<unrelated>(as_base);
        CHECK(!wrong && as_base.use_count() == 4);

        // A failed rvalue cast leaves the caller owning the object.
        auto kept = own::dynamic_owner_cast<unrelated>(std::move(as_base));
        CHECK(!kept && as_base && as_base.use_count() == 4);

        // A successful rvalue cast transfers the reference without a count change.
        auto moved = own::dynamic_owner_cast<derived>(std::move(as_base));
        CHECK(moved && !as_base && moved.use_count() == 4);

        own::shared_owner<base> empty;
        CHECK(!own::static_owner_cast<derived>(empty) && !own::dynamic_owner_cast<derived>(empty));
    }
    CHECK(destroyed == 1);
}

void sole_owner_cast_keeps_release_path() {
    // An rvalue cast of the handle a factory returned is still the sole
    // reference; the object must still be destroyed exactly once.
    int destroyed = 0;
    {
        own::shared_owner<base> owner = own::make_shared<derived>(destroyed);
        auto cast = own::static_owner_cast<derived>(std::move(owner));
        CHECK(cast.use_count() == 1 && !owner);
    }
    CHECK(destroyed == 1);
}

void local_casts() {
    int destroyed = 0;
    {
        own::local_owner<base> as_base = own::make_local<derived>(destroyed);
        auto as_derived = own::dynamic_owner_cast<derived>(as_base);
        CHECK(as_derived && as_base.local_use_count() == 2 && as_base.use_count() == 1);
        auto as_other = own::static_owner_cast<other_base>(as_derived);
        CHECK(as_other->other_value == 2 && as_base.local_use_count() == 3);
        auto failed = own::dynamic_owner_cast<unrelated>(std::move(as_base));
        CHECK(!failed && as_base && as_base.local_use_count() == 3);
        auto moved = own::static_owner_cast<derived>(std::move(as_base));
        CHECK(moved && !as_base && moved.local_use_count() == 3);
    }
    CHECK(destroyed == 1);
}

void unique_dynamic_cast() {
    int destroyed = 0;
    {
        own::unique_owner<base> as_base = own::make_unique<derived>(destroyed);
        auto failed = own::dynamic_owner_cast<unrelated>(std::move(as_base));
        CHECK(!failed && as_base);
        auto as_derived = own::dynamic_owner_cast<derived>(std::move(as_base));
        CHECK(as_derived && !as_base && as_derived->other_value == 2);
        own::unique_owner<other_base> as_other = std::move(as_derived);
        auto back = own::dynamic_owner_cast<derived>(std::move(as_other));
        CHECK(back && !as_other && back->destroyed == &destroyed);
    }
    CHECK(destroyed == 1);
}

void allocated_unique_dynamic_cast_keeps_disposer() {
    int destroyed = 0;
    counting_allocator allocator;
    {
        own::allocated_unique_owner<other_base> as_other = own::allocate_unique<derived>(allocator.ref(), destroyed);
        auto failed = own::dynamic_owner_cast<unrelated>(own::allocated_unique_owner<other_base>(std::move(as_other)));
        CHECK(!failed);
        CHECK(destroyed == 1 && allocator.live == 0); // the temporary source was destroyed with its object
    }
    {
        own::allocated_unique_owner<other_base> as_other = own::allocate_unique<derived>(allocator.ref(), destroyed);
        auto as_derived = own::dynamic_owner_cast<derived>(std::move(as_other));
        CHECK(as_derived && !as_other && allocator.live == 1);
    }
    // Destroyed through the concrete disposer and returned to its allocator.
    CHECK(destroyed == 2 && allocator.live == 0 && allocator.deallocations == 2);
}

void shared_identity() {
    int destroyed = 0;
    auto owner = own::make_shared<derived>(destroyed);
    own::shared_owner<other_base> other = owner;
    own::weak_owner<base> weak = owner;
    auto local = owner.localize();
    auto cast = own::dynamic_owner_cast<derived>(other);

    const auto id = own::identity(owner);
    CHECK(id && id == own::identity(other) && id == own::identity(weak));
    CHECK(id == own::identity(local) && id == own::identity(cast));

    auto unrelated_owner = own::make_shared<derived>(destroyed);
    CHECK(own::identity(unrelated_owner) != id);
    CHECK((own::identity(unrelated_owner) < id) != (id < own::identity(unrelated_owner)));

    // A weak key stays the same after expiry while the weak handle lives.
    owner.reset(); other.reset(); local.reset(); cast.reset();
    CHECK(weak.expired() && own::identity(weak) == id && destroyed == 1);

    CHECK(!own::identity(own::shared_owner<int>{}) && !own::identity(own::weak_owner<int>{}));
    CHECK(!own::identity(own::local_owner<int>{}));
}

void exclusive_identity() {
    int destroyed = 0;
    {
        auto owner = own::make_unique<derived>(destroyed);
        const auto id = own::identity(owner);
        own::unique_owner<other_base> as_other = std::move(owner);
        CHECK(id && own::identity(as_other) == id); // the non-zero base offset is undone
        CHECK(!own::identity(owner));
    }
    counting_allocator allocator;
    {
        auto owner = own::allocate_unique<derived>(allocator.ref(), destroyed);
        const auto id = own::identity(owner);
        own::allocated_unique_owner<other_base> as_other = std::move(owner);
        CHECK(id && own::identity(as_other) == id);
    }
    CHECK(destroyed == 2 && allocator.live == 0);
}

void identity_as_container_key() {
    std::unordered_set<own::object_id> seen;
    std::unordered_map<own::object_id, int> names;
    auto first = own::make_shared<int>(1);
    auto second = own::make_shared<int>(2);
    own::weak_owner<int> watcher = first;
    CHECK(seen.insert(own::identity(first)).second);
    CHECK(!seen.insert(own::identity(watcher)).second); // same object
    CHECK(seen.insert(own::identity(second)).second);
    names[own::identity(first)] = 10;
    CHECK(names.at(own::identity(watcher)) == 10);
    CHECK(std::hash<own::object_id>{}(own::identity(first)) == own::identity(first).hash());
    static_assert(std::is_trivially_copyable_v<own::object_id>);
    static_assert(sizeof(own::object_id) == sizeof(std::uintptr_t));
}

void weak_converting_move() {
    int destroyed = 0;
    counting_allocator allocator;
    {
        auto owner = own::allocate_shared<derived>(allocator.ref(), destroyed);
        own::weak_owner<derived> source = owner;
        own::weak_owner<other_base> target(std::move(source));
        CHECK(source.expired() && !source.lock());
        auto locked = target.lock();
        CHECK(locked && locked->other_value == 2 && owner.use_count() == 2);
        locked.reset();

        // Converting an expired weak handle never adjusts the dangling pointer.
        own::weak_owner<derived> second = owner;
        owner.reset();
        CHECK(destroyed == 1 && allocator.live == 1);
        own::weak_owner<other_base> expired(std::move(second));
        CHECK(expired.expired() && !expired.lock());
        target.reset();
        CHECK(allocator.live == 1); // `expired` still holds the block
    }
    // No weak reference was leaked or double-released by the moves.
    CHECK(allocator.live == 0 && allocator.deallocations == 1);
}

void borrowed_is_the_view_type() {
    auto owner = own::make_shared<int>(4);
    own::borrowed<const int> view = owner.borrow();
    own::local_view<const int> old_name = view;
    static_assert(std::is_same_v<own::local_view<int>, own::borrowed<int>>);
    static_assert(std::is_trivially_copyable_v<own::borrowed<int>>);
    CHECK(*old_name == 4);
}
} // namespace

int main() {
    test::run("shared static and dynamic casts", shared_static_and_dynamic_casts);
    test::run("sole-owner cast keeps release path", sole_owner_cast_keeps_release_path);
    test::run("local casts", local_casts);
    test::run("unique dynamic cast", unique_dynamic_cast);
    test::run("allocated unique dynamic cast keeps disposer", allocated_unique_dynamic_cast_keeps_disposer);
    test::run("shared identity", shared_identity);
    test::run("exclusive identity", exclusive_identity);
    test::run("identity as container key", identity_as_container_key);
    test::run("weak converting move", weak_converting_move);
    test::run("borrowed is the view type", borrowed_is_the_view_type);
    return test::finish();
}

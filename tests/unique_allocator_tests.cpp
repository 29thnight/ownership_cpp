// unique_owner<T, Alloc>: the allocator as a type argument, and construction
// through the owner's constructor.
#include <own/ownership.hpp>

#include "test_support.hpp"

#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace {
// A pool the tests can inspect. Owners refer to it through a handle.
struct pool_state {
    int live = 0;
    int allocations = 0;
    int deallocations = 0;
    std::size_t last_size = 0;
    std::size_t last_alignment = 0;
    bool fail_next = false;
};

// Stateful allocator: one pointer, stored in each owner.
struct pool_handle : own::allocator_interface<pool_handle> {
    pool_state* pool = nullptr;
    pool_handle() noexcept = default;
    explicit pool_handle(pool_state* state) noexcept : pool(state) {}
    void* do_allocate(std::size_t size, std::size_t alignment) {
        if (pool->fail_next) { pool->fail_next = false; return nullptr; }
        ++pool->live; ++pool->allocations;
        pool->last_size = size; pool->last_alignment = alignment;
        return ::operator new(size, std::align_val_t(alignment));
    }
    void do_deallocate(void* pointer, std::size_t size, std::size_t alignment) noexcept {
        --pool->live; ++pool->deallocations;
        pool->last_size = size; pool->last_alignment = alignment;
        ::operator delete(pointer, std::align_val_t(alignment));
    }
};

// Stateless allocator: no bytes in the owner.
inline int global_live = 0;
struct global_counter : own::allocator_interface<global_counter> {
    void* do_allocate(std::size_t size, std::size_t alignment) {
        ++global_live;
        return ::operator new(size, std::align_val_t(alignment));
    }
    void do_deallocate(void* pointer, std::size_t, std::size_t alignment) noexcept {
        --global_live;
        ::operator delete(pointer, std::align_val_t(alignment));
    }
};

struct mesh {
    int vertices;
    std::string name;
    int* destroyed;
    mesh(int count, std::string label, int& counter) : vertices(count), name(std::move(label)), destroyed(&counter) {}
    ~mesh() { ++*destroyed; }
};
struct alignas(64) aligned_mesh { int value = 7; };
struct throwing { explicit throwing(bool fail) { if (fail) throw std::runtime_error("ctor"); } };

struct base { virtual ~base() noexcept = default; };
struct derived : base { int extra = 3; };

// Sizes: the allocator is part of the handle only when it has state.
static_assert(sizeof(own::unique_owner<mesh>) == sizeof(void*));
static_assert(sizeof(own::unique_owner<mesh, global_counter>) == sizeof(void*));
static_assert(sizeof(own::unique_owner<mesh, pool_handle>) == 2 * sizeof(void*));
static_assert(std::is_same_v<own::unique_owner<mesh>::allocator_type, own::new_delete>);
static_assert(std::is_same_v<own::unique_owner<mesh, pool_handle>::allocator_type, pool_handle>);

// Construction needs the tag or an allocator: a bare argument list never
// adopts or silently copies an existing object.
static_assert(std::is_constructible_v<own::unique_owner<int>, std::in_place_t, int>);
static_assert(!std::is_constructible_v<own::unique_owner<int>, int>);
static_assert(!std::is_constructible_v<own::unique_owner<int>, int&>);
static_assert(!std::is_constructible_v<own::unique_owner<int>, int*>);
static_assert(!std::is_convertible_v<pool_handle, own::unique_owner<int, pool_handle>>); // explicit
// A stateful allocator has no default instance to construct with.
static_assert(!std::is_constructible_v<own::unique_owner<int, pool_handle>, std::in_place_t, int>);
static_assert(std::is_constructible_v<own::unique_owner<int, global_counter>, std::in_place_t, int>);
// Owners of different allocators are different types and never convert.
static_assert(!std::is_constructible_v<own::unique_owner<int>, own::unique_owner<int, pool_handle>&&>);
static_assert(!std::is_constructible_v<own::unique_owner<int, pool_handle>, own::unique_owner<int>&&>);
static_assert(!std::is_constructible_v<own::unique_owner<int, global_counter>, own::unique_owner<int, pool_handle>&&>);
// A user allocator releases sizeof(T): no base conversion, const only.
static_assert(!std::is_constructible_v<own::unique_owner<base, pool_handle>, own::unique_owner<derived, pool_handle>&&>);
static_assert(std::is_constructible_v<own::unique_owner<const int, pool_handle>, own::unique_owner<int, pool_handle>&&>);
// The default and allocator_ref owners keep their conversions.
static_assert(std::is_constructible_v<own::unique_owner<base>, own::unique_owner<derived>&&>);
static_assert(std::is_constructible_v<own::unique_owner<base, own::allocator_ref>, own::unique_owner<derived, own::allocator_ref>&&>);
// The concept requires the interface, not just matching member names.
struct same_members_no_interface {
    void* do_allocate(std::size_t size, std::size_t) { return ::operator new(size); }
    void do_deallocate(void* p, std::size_t, std::size_t) noexcept { ::operator delete(p); }
};
struct interface_without_noexcept : own::allocator_interface<interface_without_noexcept> {
    void* do_allocate(std::size_t size, std::size_t) { return ::operator new(size); }
    void do_deallocate(void* p, std::size_t, std::size_t) { ::operator delete(p); }
};
struct names_another_type : own::allocator_interface<pool_handle> {
    void* do_allocate(std::size_t size, std::size_t) { return ::operator new(size); }
    void do_deallocate(void* p, std::size_t, std::size_t) noexcept { ::operator delete(p); }
};
static_assert(own::unique_allocator<pool_handle> && own::unique_allocator<global_counter>);
static_assert(!own::unique_allocator<same_members_no_interface>);
static_assert(!own::unique_allocator<interface_without_noexcept>);
static_assert(!own::unique_allocator<names_another_type>);
static_assert(!own::unique_allocator<std::allocator<int>>);
// The interface adds no bytes: an allocator without data stays empty.
static_assert(std::is_empty_v<global_counter> && sizeof(pool_handle) == sizeof(void*));

void constructor_with_default_allocation() {
    int destroyed = 0;
    {
        own::unique_owner<mesh> owner(std::in_place, 12, "rock", destroyed);
        CHECK(owner && owner->vertices == 12 && owner->name == "rock");
        auto view = owner.borrow();
        CHECK(view->vertices == 12);
    }
    CHECK(destroyed == 1);
}

void constructor_with_stateful_allocator() {
    pool_state pool;
    int destroyed = 0;
    {
        own::unique_owner<mesh, pool_handle> owner(pool_handle{&pool}, 3, "tree", destroyed);
        CHECK(owner && owner->name == "tree" && pool.live == 1);
        CHECK(pool.last_size == sizeof(mesh) && pool.last_alignment == alignof(mesh));
        CHECK(owner.get_allocator().pool == &pool);

        auto moved = std::move(owner); // the allocator travels with the object
        CHECK(!owner && moved && moved.get_allocator().pool == &pool && pool.live == 1);

        own::unique_owner<const mesh, pool_handle> as_const = std::move(moved);
        CHECK(as_const->vertices == 3 && pool.live == 1);
    }
    CHECK(destroyed == 1 && pool.live == 0 && pool.deallocations == 1);
    CHECK(pool.last_size == sizeof(mesh) && pool.last_alignment == alignof(mesh));
}

void owners_from_two_pools() {
    pool_state first, second;
    int destroyed = 0;
    {
        own::unique_owner<mesh, pool_handle> a(pool_handle{&first}, 1, "a", destroyed);
        own::unique_owner<mesh, pool_handle> b(pool_handle{&second}, 2, "b", destroyed);
        swap(a, b); // objects and their allocators swap together
        CHECK(a->name == "b" && a.get_allocator().pool == &second);
        CHECK(b->name == "a" && b.get_allocator().pool == &first);
        a = std::move(b); // a's old object returns to the second pool
        CHECK(destroyed == 1 && second.live == 0 && first.live == 1);
        CHECK(a->name == "a" && a.get_allocator().pool == &first);
        a.reset();
        CHECK(destroyed == 2 && first.live == 0);
    }
    CHECK(destroyed == 2);
}

void stateless_allocator_and_factory() {
    global_live = 0;
    {
        own::unique_owner<aligned_mesh, global_counter> a(std::in_place);
        auto b = own::allocate_unique<aligned_mesh>(global_counter{});
        static_assert(std::is_same_v<decltype(b), own::unique_owner<aligned_mesh, global_counter>>);
        CHECK(global_live == 2 && a->value == 7 && b->value == 7);
        CHECK(reinterpret_cast<std::uintptr_t>(&*a) % alignof(aligned_mesh) == 0);
        pool_state pool;
        auto c = own::allocate_unique<int>(pool_handle{&pool}, 5);
        static_assert(std::is_same_v<decltype(c), own::unique_owner<int, pool_handle>>);
        CHECK(*c == 5 && pool.live == 1);
        c.reset();
        CHECK(pool.live == 0);
    }
    CHECK(global_live == 0);
}

void allocation_failure_and_constructor_failure() {
    pool_state pool;
    pool.fail_next = true;
    bool caught = false;
    try { own::unique_owner<int, pool_handle> owner(pool_handle{&pool}, 1); }
    catch (const std::bad_alloc&) { caught = true; }
    CHECK(caught && pool.live == 0 && pool.allocations == 0);

    caught = false;
    try { own::unique_owner<throwing, pool_handle> owner(pool_handle{&pool}, true); }
    catch (const std::runtime_error&) { caught = true; }
    // The storage went back to the pool that provided it.
    CHECK(caught && pool.live == 0 && pool.allocations == 1 && pool.deallocations == 1);
}

void runtime_allocator_constructor() {
    pool_state pool;
    own::allocator_ref ref{
        &pool,
        [](void* context, std::size_t size, std::size_t alignment) -> void* {
            return pool_handle{static_cast<pool_state*>(context)}.allocate(size, alignment);
        },
        [](void* context, void* pointer, std::size_t size, std::size_t alignment) noexcept {
            pool_handle{static_cast<pool_state*>(context)}.deallocate(pointer, size, alignment);
        }};
    {
        own::unique_owner<derived, own::allocator_ref> owner(ref);
        CHECK(owner->extra == 3 && pool.live == 1);
        own::unique_owner<base, own::allocator_ref> as_base = std::move(owner); // erased disposer
        CHECK(as_base && pool.live == 1);
        own::unique_owner<int, own::allocator_ref> defaulted(std::in_place, 9);
        CHECK(*defaulted == 9 && pool.live == 1); // the default allocator_ref is not this pool
    }
    CHECK(pool.live == 0 && pool.last_size == sizeof(derived));
}

void identity_and_view() {
    pool_state pool;
    own::unique_owner<int, pool_handle> owner(pool_handle{&pool}, 4);
    const auto id = own::identity(owner);
    auto moved = std::move(owner);
    CHECK(id && own::identity(moved) == id && !own::identity(owner));
    own::borrowed<const int> view = moved.borrow();
    CHECK(*view == 4);
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
// The former name is the same type, so existing code keeps compiling.
static_assert(std::is_same_v<own::allocated_unique_owner<int>, own::unique_owner<int, own::allocator_ref>>);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
} // namespace

int main() {
    test::run("constructor with default allocation", constructor_with_default_allocation);
    test::run("constructor with stateful allocator", constructor_with_stateful_allocator);
    test::run("owners from two pools", owners_from_two_pools);
    test::run("stateless allocator and factory", stateless_allocator_and_factory);
    test::run("allocation failure and constructor failure", allocation_failure_and_constructor_failure);
    test::run("runtime allocator constructor", runtime_allocator_constructor);
    test::run("identity and view", identity_and_view);
    return test::finish();
}

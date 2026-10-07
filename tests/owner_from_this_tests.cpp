#include <own/ownership.hpp>

#include "test_support.hpp"

#include <atomic>
#include <memory>
#include <vector>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

namespace {
struct observations {
    int destroyed = 0;
    bool constructor_unbound = false;
    bool destructor_unbound = false;
};

struct self_owned : own::enable_owner_from_this<self_owned> {
    observations* observed;
    int value = 73;
    explicit self_owned(observations& state) : observed(&state) {
        state.constructor_unbound = !shared_from_this() && weak_from_this().expired() &&
                                   !local_from_this() && !borrow_from_this();
    }
    ~self_owned() {
        observed->destructor_unbound = !shared_from_this() && weak_from_this().expired() &&
                                      !local_from_this() && !borrow_from_this();
        ++observed->destroyed;
    }
};

struct copying_self : own::enable_owner_from_this<copying_self> {
    int value;
    explicit copying_self(int number) : value(number) {}
    copying_self(const copying_self&) = default;
    copying_self(copying_self&&) = default;
    copying_self& operator=(const copying_self&) = default;
    copying_self& operator=(copying_self&&) = default;
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

struct counting_allocator {
    int attempts = 0;
    int fail_at = -1;
    int allocated = 0;
    int deallocated = 0;
    static void* allocate(void* context, std::size_t bytes, std::size_t alignment) {
        auto& self = *static_cast<counting_allocator*>(context);
        if (self.attempts++ == self.fail_at) throw std::bad_alloc();
        ++self.allocated;
        return ::operator new(bytes, std::align_val_t(alignment));
    }
    static void deallocate(void* context, void* pointer, std::size_t,
                           std::size_t alignment) noexcept {
        ++static_cast<counting_allocator*>(context)->deallocated;
        ::operator delete(pointer, std::align_val_t(alignment));
    }
    own::allocator_ref ref() { return {this, &allocate, &deallocate}; }
};

struct offset_prefix { virtual ~offset_prefix() = default; int prefix = 17; };
struct inherited_base : own::enable_owner_from_this<inherited_base> { int base_value = 91; };
struct offset_derived : offset_prefix, inherited_base { int derived_value = 37; };
struct virtual_derived : virtual inherited_base { int value = 29; };
struct diamond_left : virtual inherited_base {};
struct diamond_right : virtual inherited_base {};
struct virtual_diamond : diamond_left, diamond_right {};

static_assert(std::is_same_v<decltype(std::declval<self_owned&>().shared_from_this()), own::shared_owner<self_owned>>);
static_assert(std::is_same_v<decltype(std::declval<const self_owned&>().shared_from_this()), own::shared_owner<const self_owned>>);
static_assert(std::is_same_v<decltype(std::declval<self_owned&>().weak_from_this()), own::weak_owner<self_owned>>);
static_assert(std::is_same_v<decltype(std::declval<const self_owned&>().weak_from_this()), own::weak_owner<const self_owned>>);
static_assert(std::is_same_v<decltype(std::declval<self_owned&>().local_from_this()), own::local_view<self_owned>>);
static_assert(std::is_same_v<decltype(std::declval<const self_owned&>().local_from_this()), own::local_view<const self_owned>>);
static_assert(std::is_same_v<decltype(std::declval<self_owned&>().borrow_from_this()), own::local_view<self_owned>>);
static_assert(std::is_same_v<decltype(std::declval<const self_owned&>().borrow_from_this()), own::local_view<const self_owned>>);
static_assert(noexcept(std::declval<self_owned&>().shared_from_this()));
static_assert(noexcept(std::declval<self_owned&>().weak_from_this()));
template<class T> concept rvalue_local_from_this = requires(T&& object) { std::move(object).local_from_this(); };
template<class T> concept rvalue_borrow_from_this = requires(T&& object) { std::move(object).borrow_from_this(); };
static_assert(!rvalue_local_from_this<self_owned> && !rvalue_local_from_this<const self_owned>);
static_assert(!rvalue_borrow_from_this<self_owned> && !rvalue_borrow_from_this<const self_owned>);

void unbound_stack_and_construction() {
    observations observed;
    {
        self_owned unmanaged(observed);
        CHECK(observed.constructor_unbound);
        CHECK(!unmanaged.shared_from_this() && unmanaged.weak_from_this().expired());
        CHECK(!unmanaged.local_from_this() && !unmanaged.borrow_from_this());
        const auto& constant = unmanaged;
        CHECK(!constant.shared_from_this() && constant.weak_from_this().expired());
        CHECK(!constant.local_from_this() && !constant.borrow_from_this());
    }
    CHECK(observed.destroyed == 1 && observed.destructor_unbound);
}

void same_control_and_independent_ownership() {
    observations observed;
    counting_allocator allocation;
    own::weak_owner<self_owned> weak;
    {
        auto owner = own::allocate_shared<self_owned>(allocation.ref(), observed);
        CHECK(observed.constructor_unbound && owner.use_count() == 1);
        auto shared = owner->shared_from_this();
        weak = owner->weak_from_this();
        CHECK(std::addressof(*shared) == std::addressof(*owner));
        CHECK(shared.use_count() == 2 && owner.use_count() == 2 && weak.use_count() == 2);
        auto borrowed = owner->borrow_from_this();
        auto local = owner->local_from_this();
        CHECK(std::addressof(*borrowed) == std::addressof(*owner));
        CHECK(local->value == 73 && owner.use_count() == 2 && allocation.allocated == 1);
        owner.reset();
        CHECK(shared.use_count() == 1 && shared->value == 73 && observed.destroyed == 0);
        CHECK(borrowed->value == 73); // shared retains the payload throughout this scope.
    }
    CHECK(observed.destroyed == 1 && observed.destructor_unbound && weak.expired());
    CHECK(allocation.deallocated == 0);
    weak.reset();
    CHECK(allocation.allocated == 1 && allocation.deallocated == 1);
}

void optional_local_owner_registration() {
    observations observed;
    counting_allocator allocation;
    own::shared_owner<self_owned> escaped;
    {
        auto local = own::allocate_local<self_owned>(allocation.ref(), observed);
        auto alias = local;
        escaped = local->shared_from_this();
        auto weak = alias->weak_from_this();
        auto borrowed = alias->local_from_this();
        CHECK(local.local_use_count() == 2 && local.use_count() == 2 && weak.use_count() == 2);
        CHECK(borrowed->value == 73 && allocation.allocated == 2);
    }
    CHECK(observed.destroyed == 0 && escaped.use_count() == 1 && allocation.deallocated == 1);
    escaped.reset();
    CHECK(observed.destroyed == 1 && observed.destructor_unbound && allocation.deallocated == 2);
}

void const_factory_and_access() {
    observations observed;
    {
        auto owner = own::make_shared<const self_owned>(observed);
        auto shared = owner->shared_from_this();
        auto weak = owner->weak_from_this();
        auto borrowed = owner->local_from_this();
        CHECK(std::addressof(*shared) == std::addressof(*owner));
        CHECK(shared.use_count() == 2 && weak.use_count() == 2 && borrowed->value == 73);
        CHECK(owner->borrow_from_this()->value == 73);
    }
    CHECK(observed.destroyed == 1 && observed.destructor_unbound);
    {
        auto owner = own::make_local<const self_owned>(observed);
        auto shared = owner->shared_from_this();
        CHECK(shared.use_count() == 2 && owner->borrow_from_this()->value == 73);
    }
    CHECK(observed.destroyed == 2);
}

void copying_and_moving_discard_registration() {
    auto original = own::make_shared<copying_self>(19);
    copying_self copied(*original);
    copying_self moved(std::move(*original));
    CHECK(!copied.shared_from_this() && copied.weak_from_this().expired() && !copied.local_from_this());
    CHECK(!moved.shared_from_this() && moved.weak_from_this().expired() && !moved.borrow_from_this());
    CHECK(original.use_count() == 1 && original->shared_from_this().use_count() == 2);
    auto copy_owner = own::make_shared<copying_self>(*original);
    auto move_owner = own::make_shared<copying_self>(std::move(*original));
    auto copy_self = copy_owner->shared_from_this();
    auto move_self = move_owner->shared_from_this();
    CHECK(std::addressof(*copy_self) == std::addressof(*copy_owner));
    CHECK(std::addressof(*move_self) == std::addressof(*move_owner));
    CHECK(std::addressof(*copy_self) != std::addressof(*original));
    CHECK(std::addressof(*move_self) != std::addressof(*original));
    CHECK(copy_owner.use_count() == 2 && move_owner.use_count() == 2 && original.use_count() == 1);
}

void assignments_preserve_destination_identity() {
    auto source = own::make_shared<copying_self>(17);
    auto target = own::make_shared<copying_self>(29);
    auto* target_address = std::addressof(*target);
    *target = *source;
    CHECK(std::addressof(*target->shared_from_this()) == target_address && target->value == 17);
    CHECK(source.use_count() == 1 && target.use_count() == 1);
    *target = std::move(*source);
    CHECK(std::addressof(*target->shared_from_this()) == target_address);
    CHECK(std::addressof(*source->shared_from_this()) == std::addressof(*source));
    copying_self unmanaged(41);
    unmanaged = *source;
    CHECK(!unmanaged.shared_from_this());
    *target = unmanaged;
    CHECK(std::addressof(*target->shared_from_this()) == target_address);
    CHECK(source.use_count() == 1 && target.use_count() == 1);
}

void inherited_mixins_adjust_pointer() {
    auto owner = own::make_shared<offset_derived>();
    auto* base = static_cast<inherited_base*>(std::addressof(*owner));
    CHECK(static_cast<void*>(base) != static_cast<void*>(std::addressof(*owner)));
    auto shared = owner->shared_from_this();
    auto borrowed = owner->local_from_this();
    auto weak = owner->weak_from_this();
    CHECK(std::addressof(*shared) == base && std::addressof(*borrowed) == base);
    CHECK(owner.use_count() == 2 && weak.use_count() == 2);
    owner.reset();
    CHECK(shared->base_value == 91 && shared.use_count() == 1);
    auto virtual_owner = own::make_shared<virtual_derived>();
    auto virtual_shared = virtual_owner->shared_from_this();
    CHECK(std::addressof(*virtual_shared) == static_cast<inherited_base*>(std::addressof(*virtual_owner)));
    CHECK(virtual_owner.use_count() == 2 && virtual_owner->borrow_from_this()->base_value == 91);
    auto diamond = own::make_shared<virtual_diamond>();
    auto diamond_shared = diamond->shared_from_this();
    CHECK(std::addressof(*diamond_shared) == static_cast<inherited_base*>(std::addressof(*diamond)));
    CHECK(diamond.use_count() == 2 && diamond->local_from_this()->base_value == 91);
}

void deferred_expiry_never_resurrects() {
    for (bool local : {false, true}) {
        observations observed;
        counting_allocator allocation;
        deferred_queue queue;
        own::weak_owner<self_owned> weak;
        self_owned* payload = nullptr;
        if (local) {
            auto owner = own::allocate_local_with<self_owned>(allocation.ref(), queue.hook(), observed);
            payload = std::addressof(*owner);
            weak = owner;
        } else {
            auto owner = own::allocate_shared_with<self_owned>(allocation.ref(), queue.hook(), observed);
            payload = std::addressof(*owner);
            weak = owner;
        }
        CHECK(queue.calls == 1 && weak.expired() && observed.destroyed == 0);
        // The queued retirement task explicitly keeps this concrete payload
        // alive until run(). These accesses are not through a dangling view.
        CHECK(!payload->shared_from_this() && payload->weak_from_this().expired());
        CHECK(!payload->local_from_this() && !payload->borrow_from_this());
        queue.pending->run();
        CHECK(observed.destroyed == 1 && observed.destructor_unbound && !weak.lock());
        queue.pending.reset();
        weak.reset();
        CHECK(allocation.allocated == (local ? 2 : 1));
        CHECK(allocation.allocated == allocation.deallocated);
    }
}

void failed_factory_cleans_registration() {
    observations observed;
    counting_allocator allocation;
    deferred_queue queue;
    allocation.fail_at = 1; // Optional local-owner group allocation, after binding.
    bool caught = false;
    try {
        auto owner = own::allocate_local_with<self_owned>(allocation.ref(), queue.hook(), observed);
        (void)owner;
    } catch (const std::bad_alloc&) { caught = true; }
    CHECK(caught && observed.constructor_unbound && observed.destructor_unbound);
    CHECK(observed.destroyed == 1 && queue.calls == 0);
    CHECK(allocation.allocated == 1 && allocation.deallocated == 1);

    struct throwing_self : own::enable_owner_from_this<throwing_self> {
        explicit throwing_self(bool& unbound) {
            unbound = !shared_from_this() && weak_from_this().expired() && !local_from_this();
            throw std::runtime_error("expected constructor failure");
        }
    };
    allocation.fail_at = -1;
    bool unbound = false;
    caught = false;
    try {
        auto owner = own::allocate_shared_with<throwing_self>(allocation.ref(), queue.hook(), unbound);
        (void)owner;
    } catch (const std::runtime_error&) { caught = true; }
    CHECK(caught && unbound && queue.calls == 0);
    CHECK(allocation.allocated == 2 && allocation.deallocated == 2);
}

void concurrent_from_this_uses_original_block() {
    observations observed;
    std::atomic<int> successes{0};
    auto source = own::make_shared<self_owned>(observed);
    own::weak_owner<self_owned> weak = source;
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) {
        workers.emplace_back([source, &successes] {
            for (int iteration = 0; iteration < 10000; ++iteration) {
                auto owner = source->shared_from_this();
                auto observer = source->weak_from_this();
                auto local = source->local_from_this();
                auto locked = observer.lock();
                if (owner->value == 73 && local->value == 73 &&
                    std::addressof(*locked) == std::addressof(*source)) {
                    successes.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    source.reset();
    for (auto& worker : workers) worker.join();
    CHECK(successes == 40000 && observed.destroyed == 1 && observed.destructor_unbound);
    CHECK(weak.expired() && !weak.lock());
}

void destruction_can_move_threads() {
    observations observed;
    own::weak_owner<self_owned> weak;
    auto owner = own::make_shared<self_owned>(observed);
    weak = owner->weak_from_this();
    auto escaped = owner->shared_from_this();
    owner.reset();
    std::thread worker([owner = std::move(escaped)]() mutable {
        auto copy = owner->shared_from_this();
        owner.reset();
        copy.reset();
    });
    worker.join();
    CHECK(observed.destroyed == 1 && observed.destructor_unbound && weak.expired());
}
} // namespace

int main() {
    test::run("from-this unmanaged and construction states", unbound_stack_and_construction);
    test::run("from-this shares original control block", same_control_and_independent_ownership);
    test::run("from-this optional local-owner registration", optional_local_owner_registration);
    test::run("from-this const payload factories", const_factory_and_access);
    test::run("from-this copy and move fresh registration", copying_and_moving_discard_registration);
    test::run("from-this assignment preserves destination", assignments_preserve_destination_identity);
    test::run("from-this inherited offset and virtual bases", inherited_mixins_adjust_pointer);
    test::run("from-this deferred expiry never resurrects", deferred_expiry_never_resurrects);
    test::run("from-this failed factories clean registration", failed_factory_cleans_registration);
    test::run("from-this concurrent original-block access", concurrent_from_this_uses_original_block);
    test::run("from-this destruction on another thread", destruction_can_move_threads);
    return test::finish();
}

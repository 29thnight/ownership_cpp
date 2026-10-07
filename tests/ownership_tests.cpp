#include <own/ownership.hpp>

#include "test_support.hpp"

#include <array>
#include <atomic>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
struct tracked {
    std::atomic<int>* destroyed;
    int value;
    explicit tracked(std::atomic<int>& count, int number = 42)
        : destroyed(&count), value(number) {}
    ~tracked() { destroyed->fetch_add(1, std::memory_order_relaxed); }
};

struct allocation_record {
    void* pointer;
    std::size_t bytes;
    std::size_t alignment;
};

struct counting_allocator {
    std::mutex mutex;
    std::vector<allocation_record> live;
    int attempts = 0;
    int allocated = 0;
    int deallocated = 0;
    int fail_at = -1;
    bool return_null = false;
    bool mismatch = false;

    static void* allocate(void* context, std::size_t bytes, std::size_t alignment) {
        auto& self = *static_cast<counting_allocator*>(context);
        std::lock_guard lock(self.mutex);
        const int attempt = self.attempts++;
        if (attempt == self.fail_at) {
            if (self.return_null) return nullptr;
            throw std::bad_alloc();
        }
        void* pointer = ::operator new(bytes, std::align_val_t(alignment));
        self.live.push_back({pointer, bytes, alignment});
        ++self.allocated;
        return pointer;
    }

    static void deallocate(void* context, void* pointer, std::size_t bytes,
                           std::size_t alignment) noexcept {
        auto& self = *static_cast<counting_allocator*>(context);
        std::lock_guard lock(self.mutex);
        bool found = false;
        for (auto it = self.live.begin(); it != self.live.end(); ++it) {
            if (it->pointer == pointer) {
                found = true;
                self.mismatch |= it->bytes != bytes || it->alignment != alignment;
                // Free according to the original allocation, even after reporting a mismatch.
                const auto original_alignment = it->alignment;
                self.live.erase(it);
                ::operator delete(pointer, std::align_val_t(original_alignment));
                break;
            }
        }
        self.mismatch |= !found;
        ++self.deallocated;
    }

    own::allocator_ref ref() { return {this, &allocate, &deallocate}; }
    void check_balanced() {
        CHECK(!mismatch);
        CHECK(live.empty());
        CHECK(allocated == deallocated);
    }
};

template <class F> void expect_bad_alloc(F&& function) {
    bool caught = false;
    try { function(); } catch (const std::bad_alloc&) { caught = true; }
    CHECK(caught);
}

struct deferred_queue {
    std::optional<own::retirement_task> pending;
    int calls = 0;
    static void retire(void* context, own::retirement_task task) noexcept {
        auto& self = *static_cast<deferred_queue*>(context);
        ++self.calls;
        self.pending.emplace(std::move(task));
    }
    own::retirement_hook hook() { return {this, &retire}; }
};

struct left_base { int left = 17; };
struct right_base { int right = 31; };
struct derived : left_base, right_base {
    std::atomic<int>* destroyed;
    explicit derived(std::atomic<int>& count) : destroyed(&count) {}
    ~derived() { destroyed->fetch_add(1, std::memory_order_relaxed); }
};

struct virtual_base {
    int value = 91;
    virtual ~virtual_base() = default;
};
struct virtual_branch : virtual virtual_base { int padding = 7; };
struct virtual_derived : virtual_branch {
    std::atomic<int>* destroyed;
    explicit virtual_derived(std::atomic<int>& count) : destroyed(&count) {}
    ~virtual_derived() override { ++*destroyed; }
};

struct alignas(512) over_aligned {
    std::atomic<int>* destroyed;
    std::array<std::byte, 513> bytes{};
    explicit over_aligned(std::atomic<int>& count) : destroyed(&count) {}
    ~over_aligned() { ++*destroyed; }
};

struct throwing_value {
    struct guard {
        std::atomic<int>* destroyed;
        ~guard() { ++*destroyed; }
    } member;
    explicit throwing_value(std::atomic<int>& count) : member{&count} {
        throw std::runtime_error("expected constructor failure");
    }
};

static_assert(std::is_copy_constructible_v<own::local_owner<int>>);
static_assert(std::is_copy_constructible_v<own::shared_owner<int>>);
static_assert(std::is_copy_constructible_v<own::weak_owner<int>>);
static_assert(std::is_nothrow_move_constructible_v<own::shared_owner<int>>);
static_assert(std::is_nothrow_move_constructible_v<own::weak_owner<int>>);
static_assert(!std::is_constructible_v<own::local_owner<int>, int*>);
static_assert(!std::is_constructible_v<own::shared_owner<int>, int*>);
static_assert(!std::is_convertible_v<own::local_owner<int>, own::shared_owner<int>>);
static_assert(!std::is_constructible_v<own::shared_owner<int>, own::local_owner<int>>);
static_assert(std::is_convertible_v<own::shared_owner<derived>, own::shared_owner<right_base>>);
static_assert(std::is_convertible_v<own::local_owner<derived>, own::local_owner<right_base>>);
static_assert(std::is_convertible_v<own::shared_owner<int>, own::shared_owner<const int>>);
static_assert(!std::is_convertible_v<own::shared_owner<const int>, own::shared_owner<int>>);
static_assert(!std::is_convertible_v<own::local_owner<const int>, own::local_owner<int>>);
static_assert(!std::is_copy_constructible_v<own::retirement_task>);
static_assert(std::is_nothrow_move_constructible_v<own::retirement_task>);
static_assert(noexcept(std::declval<own::weak_owner<int>>().lock()));

void empty_owners() {
    own::shared_owner<int> shared;
    own::local_owner<int> local;
    own::weak_owner<int> weak;
    CHECK(!shared && !local);
    CHECK(!shared.borrow() && !local.borrow());
    CHECK(shared.use_count() == 0 && local.use_count() == 0);
    CHECK(local.local_use_count() == 0);
    CHECK(weak.expired() && weak.use_count() == 0 && !weak.lock());
    CHECK(!local.share());
    CHECK(!shared.localize());
    shared.reset();
    local.reset();
    weak.reset();
    auto shared_copy = shared;
    auto local_copy = local;
    shared.swap(shared_copy);
    local.swap(local_copy);
    CHECK(!shared_copy && !local_copy);
}

void shared_lifetime() {
    std::atomic<int> destroyed{0};
    {
        auto first = own::make_shared<tracked>(destroyed, 123);
        CHECK(first->value == 123 && (*first).value == 123);
        CHECK(first.use_count() == 1);
        auto second = first;
        CHECK(first.use_count() == 2 && std::addressof(*second) == std::addressof(*first));
        auto third = std::move(second);
        CHECK(!second && third.use_count() == 2);
        own::shared_owner<tracked> fourth;
        fourth = third;
        CHECK(first.use_count() == 3);
        third.reset();
        CHECK(first.use_count() == 2);
        auto& fourth_alias = fourth; // Self-assignment through an alias keeps Clang's warning quiet.
        fourth = fourth_alias;
        CHECK(std::addressof(*fourth) == std::addressof(*first));
        second = std::move(fourth);
        CHECK(!fourth);
        first.swap(second);
        CHECK(std::addressof(*first) == std::addressof(*second));
        second.reset();
        CHECK(destroyed == 0 && first.use_count() == 1);
    }
    CHECK(destroyed == 1);
}

void local_lifetime_and_promotion() {
    std::atomic<int> destroyed{0};
    own::shared_owner<tracked> escaped;
    {
        auto first = own::make_local<tracked>(destroyed);
        CHECK(first.use_count() == 1 && first.local_use_count() == 1);
        auto second = first;
        auto third = second;
        CHECK(first.local_use_count() == 3 && first.use_count() == 1);
        CHECK(std::addressof(*second) == std::addressof(*first) && (*third).value == 42);
        escaped = second.share();
        CHECK(std::addressof(*escaped) == std::addressof(*first) && first.use_count() == 2);
        second.reset();
        CHECK(first.local_use_count() == 2);
        own::local_owner<tracked> fourth;
        fourth = first;
        CHECK(first.local_use_count() == 3);
        auto& fourth_alias = fourth;
        fourth = fourth_alias;
        third = std::move(fourth);
        CHECK(!fourth && first.local_use_count() == 2);
        first.swap(third);
        CHECK(std::addressof(*first) == std::addressof(*third));
    }
    CHECK(destroyed == 0 && escaped.use_count() == 1);
    escaped.reset();
    CHECK(destroyed == 1);
}

void independent_local_groups() {
    std::atomic<int> destroyed{0};
    auto shared = own::make_shared<tracked>(destroyed);
    auto first = shared.localize();
    auto second = shared.localize();
    CHECK(shared.use_count() == 3);
    auto first_alias = first;
    CHECK(first.local_use_count() == 2 && first_alias.local_use_count() == 2);
    CHECK(second.local_use_count() == 1);
    auto escaped = first.share();
    CHECK(shared.use_count() == 4);
    shared.reset();
    second.reset();
    escaped.reset();
    CHECK(first.use_count() == 1 && destroyed == 0);
    first.reset();
    CHECK(first_alias.local_use_count() == 1 && destroyed == 0);
    first_alias.reset();
    CHECK(destroyed == 1);
}

void rvalue_transitions() {
    std::atomic<int> destroyed{0};
    {
        auto shared = own::make_shared<tracked>(destroyed);
        auto local = std::move(shared).localize();
        CHECK(local && local->value == 42);
        auto alias = local;
        auto promoted = std::move(local).share();
        CHECK(promoted && std::addressof(*promoted) == std::addressof(*alias));
        alias.reset();
        CHECK(destroyed == 0);
        auto last_local = std::move(promoted).localize();
        auto last_shared = std::move(last_local).share();
        CHECK(last_shared && last_shared->value == 42);
    }
    CHECK(destroyed == 1);
}

void weak_semantics() {
    std::atomic<int> destroyed{0};
    own::weak_owner<tracked> weak;
    {
        auto local = own::make_local<tracked>(destroyed);
        weak = own::weak_owner<tracked>(local);
        CHECK(!weak.expired() && weak.use_count() == 1);
        auto alias = local;
        CHECK(weak.use_count() == 1);
        auto locked = weak.lock();
        CHECK(std::addressof(*locked) == std::addressof(*local) && weak.use_count() == 2);
        auto copy = weak;
        auto moved = std::move(copy);
        CHECK(copy.expired());
        local.reset();
        alias.reset();
        CHECK(destroyed == 0 && weak.use_count() == 1);
        locked.reset();
        CHECK(destroyed == 1 && moved.expired());
        CHECK(!moved.lock());
        moved.swap(weak);
    }
    CHECK(weak.expired() && !weak.lock() && weak.use_count() == 0);
    weak.reset();
}

void shared_conversion_adjusts_pointer() {
    std::atomic<int> destroyed{0};
    own::weak_owner<const right_base> weak;
    {
        auto original = own::make_shared<derived>(destroyed);
        auto* adjusted = static_cast<right_base*>(std::addressof(*original));
        CHECK(static_cast<void*>(adjusted) != static_cast<void*>(std::addressof(*original)));
        own::shared_owner<right_base> base = original;
        CHECK(std::addressof(*base) == adjusted && base->right == 31);
        own::shared_owner<const right_base> constant = std::move(base);
        CHECK(!base && std::addressof(*constant) == adjusted);
        weak = own::weak_owner<const right_base>(original);
        auto locked = weak.lock();
        CHECK(std::addressof(*locked) == adjusted);
        own::shared_owner<const right_base> assigned;
        assigned = original;
        CHECK(std::addressof(*assigned) == adjusted);
        original.reset();
        CHECK(destroyed == 0);
    }
    CHECK(destroyed == 1 && weak.expired());
}

void local_conversion_adjusts_pointer() {
    std::atomic<int> destroyed{0};
    {
        auto original = own::make_local<derived>(destroyed);
        auto* adjusted = static_cast<right_base*>(std::addressof(*original));
        own::local_owner<right_base> base = original;
        CHECK(std::addressof(*base) == adjusted && base->right == 31);
        own::local_owner<const right_base> constant = std::move(base);
        CHECK(!base && std::addressof(*constant) == adjusted);
        own::weak_owner<const right_base> weak = original;
        CHECK(std::addressof(*weak.lock()) == adjusted);
        own::local_owner<const right_base> assigned;
        assigned = original;
        CHECK(std::addressof(*assigned) == adjusted);
        auto escaped = constant.share();
        CHECK(std::addressof(*escaped) == adjusted);
        auto relocalized = escaped.localize();
        CHECK(std::addressof(*relocalized) == adjusted);
        CHECK(original.local_use_count() == 3 && relocalized.local_use_count() == 1);
    }
    CHECK(destroyed == 1);
}

void immutable_and_move_only_payloads() {
    auto immutable = own::make_shared<const int>(17);
    auto local_immutable = own::make_local<const int>(29);
    CHECK(*immutable == 17 && *local_immutable == 29);
    auto unique = own::make_local<std::unique_ptr<int>>(std::make_unique<int>(43));
    CHECK(**unique == 43);
    auto shared = unique.share();
    CHECK(**shared == 43);
}

void class_specific_new_is_bypassed() {
    struct custom_new {
        static void* operator new(std::size_t) = delete;
        static void* operator new(std::size_t, void*) = delete;
        std::atomic<int>* destroyed;
        explicit custom_new(std::atomic<int>& counter) : destroyed(&counter) {}
        ~custom_new() { ++*destroyed; }
    };
    std::atomic<int> destroyed{0};
    {
        auto first = own::make_shared<custom_new>(destroyed);
        auto second = own::make_local<custom_new>(destroyed);
        CHECK(first && second);
    }
    CHECK(destroyed == 2);
}

void virtual_base_weak_conversions() {
    counting_allocator allocation;
    std::atomic<int> destroyed{0};
    own::weak_owner<virtual_derived> derived_weak;
    own::weak_owner<virtual_base> base_weak;
    {
        auto source = own::allocate_shared<virtual_derived>(allocation.ref(), destroyed);
        auto* adjusted = static_cast<virtual_base*>(std::addressof(*source));
        derived_weak = source;
        base_weak = derived_weak;
        CHECK(std::addressof(*base_weak.lock()) == adjusted);
        own::weak_owner<const virtual_base> constant_weak = derived_weak;
        CHECK(constant_weak.lock()->value == 91);
        auto derived_copy = derived_weak;
        own::weak_owner<virtual_base> moved = std::move(derived_copy);
        CHECK(derived_copy.expired() && std::addressof(*moved.lock()) == adjusted);
        own::shared_owner<virtual_base> base_shared = source;
        CHECK(std::addressof(*base_shared) == adjusted);
        auto local = source.localize();
        own::local_owner<virtual_base> base_local = local;
        CHECK(std::addressof(*base_local) == adjusted);
    }
    CHECK(destroyed == 1 && derived_weak.expired());
    // This adjustment must not dereference the destroyed derived object's vptr.
    own::weak_owner<const virtual_base> expired_copy = derived_weak;
    own::weak_owner<virtual_base> expired_move = std::move(derived_weak);
    CHECK(expired_copy.expired() && !expired_copy.lock());
    CHECK(expired_move.expired() && !expired_move.lock());
    CHECK(derived_weak.expired());
    base_weak.reset();
    expired_copy.reset();
    expired_move.reset();
    allocation.check_balanced();
}

void virtual_base_weak_conversion_race() {
    for (int repetition = 0; repetition < 75; ++repetition) {
        std::atomic<int> destroyed{0};
        std::atomic<bool> violation{false};
        auto source = own::make_shared<virtual_derived>(destroyed);
        own::weak_owner<virtual_derived> weak = source;
        std::barrier start(5);
        std::vector<std::thread> workers;
        for (int worker = 0; worker < 4; ++worker) {
            workers.emplace_back([weak, &start, &violation, &destroyed] {
                start.arrive_and_wait();
                for (int iteration = 0; iteration < 150; ++iteration) {
                    own::weak_owner<virtual_base> converted = weak;
                    auto locked = converted.lock();
                    if (locked && (locked->value != 91 || destroyed.load() != 0)) violation = true;
                }
            });
        }
        start.arrive_and_wait();
        source.reset();
        for (auto& worker : workers) worker.join();
        CHECK(destroyed == 1 && !violation && weak.expired());
    }
}

void custom_allocator_lifetime() {
    counting_allocator allocation;
    std::atomic<int> destroyed{0};
    own::weak_owner<tracked> weak;
    {
        auto shared = own::allocate_shared<tracked>(allocation.ref(), destroyed);
        weak = own::weak_owner<tracked>(shared);
        auto local = shared.localize(allocation.ref());
        auto alias = local;
        CHECK(alias->value == 42);
    }
    CHECK(destroyed == 1);
    CHECK(!allocation.live.empty());
    weak.reset();
    allocation.check_balanced();
    {
        auto local = own::allocate_local<tracked>(allocation.ref(), destroyed);
        auto alias = local;
        CHECK(std::addressof(*alias) == std::addressof(*local));
    }
    CHECK(destroyed == 2);
    allocation.check_balanced();
}

void over_aligned_storage() {
    counting_allocator allocation;
    std::atomic<int> destroyed{0};
    {
        auto first = own::make_shared<over_aligned>(destroyed);
        auto second = own::make_local<over_aligned>(destroyed);
        auto third = own::allocate_shared<over_aligned>(allocation.ref(), destroyed);
        auto fourth = own::allocate_local<over_aligned>(allocation.ref(), destroyed);
        for (const auto* pointer : {std::addressof(*first), std::addressof(*second), std::addressof(*third), std::addressof(*fourth)}) {
            CHECK(reinterpret_cast<std::uintptr_t>(pointer) % alignof(over_aligned) == 0);
            CHECK(pointer->bytes.front() == std::byte{});
        }
    }
    CHECK(destroyed == 4);
    allocation.check_balanced();
}

void throwing_constructors() {
    for (bool local : {false, true}) {
        counting_allocator allocation;
        std::atomic<int> member_destroyed{0};
        bool caught = false;
        try {
            if (local) {
                auto owner = own::allocate_local<throwing_value>(allocation.ref(), member_destroyed);
                (void)owner;
            } else {
                auto owner = own::allocate_shared<throwing_value>(allocation.ref(), member_destroyed);
                (void)owner;
            }
        } catch (const std::runtime_error&) { caught = true; }
        CHECK(caught && member_destroyed == 1);
        allocation.check_balanced();
    }
}

void initial_allocation_failures() {
    for (bool null_failure : {false, true}) {
        for (bool local : {false, true}) {
            counting_allocator allocation;
            allocation.fail_at = 0;
            allocation.return_null = null_failure;
            std::atomic<int> destroyed{0};
            expect_bad_alloc([&] {
                if (local) {
                    auto owner = own::allocate_local<tracked>(allocation.ref(), destroyed);
                    (void)owner;
                } else {
                    auto owner = own::allocate_shared<tracked>(allocation.ref(), destroyed);
                    (void)owner;
                }
            });
            CHECK(destroyed == 0);
            allocation.check_balanced();
        }
    }
}

void local_group_allocation_failures() {
    // allocate_local places its first group inside the block: exactly one
    // allocation, and failing it constructs nothing.
    for (bool null_failure : {false, true}) {
        counting_allocator allocation;
        allocation.fail_at = 0;
        allocation.return_null = null_failure;
        std::atomic<int> destroyed{0};
        expect_bad_alloc([&] {
            auto local = own::allocate_local<tracked>(allocation.ref(), destroyed);
            (void)local;
        });
        CHECK(destroyed == 0);
        allocation.check_balanced();
    }
    counting_allocator allocation;
    allocation.fail_at = 1;
    std::atomic<int> destroyed{0};
    {
        auto local = own::allocate_local<tracked>(allocation.ref(), destroyed);
        CHECK(local && allocation.allocated == 1 && local.local_use_count() == 1);
        auto copy = local;
        auto shared = local.share();
        CHECK(local.local_use_count() == 2 && shared.use_count() == 2);
    }
    CHECK(destroyed == 1);
    allocation.check_balanced();
}

void failed_factory_does_not_retire() {
    {
        counting_allocator allocation;
        allocation.fail_at = 0;
        deferred_queue queue;
        std::atomic<int> destroyed{0};
        expect_bad_alloc([&] {
            auto local = own::allocate_local_with<tracked>(allocation.ref(), queue.hook(), destroyed);
            (void)local;
        });
        CHECK(destroyed == 0);
        CHECK(queue.calls == 0 && !queue.pending);
        allocation.check_balanced();
    }
    {
        // One allocation; the hook sees the object exactly once, at last release.
        counting_allocator allocation;
        allocation.fail_at = 1;
        deferred_queue queue;
        std::atomic<int> destroyed{0};
        {
            auto local = own::allocate_local_with<tracked>(allocation.ref(), queue.hook(), destroyed);
            CHECK(allocation.allocated == 1 && queue.calls == 0);
        }
        CHECK(queue.calls == 1 && queue.pending && destroyed == 0);
        queue.pending.reset();
        CHECK(destroyed == 1);
        allocation.check_balanced();
    }
    counting_allocator allocation;
    deferred_queue queue;
    std::atomic<int> member_destroyed{0};
    bool caught = false;
    try {
        auto source = own::allocate_shared_with<throwing_value>(allocation.ref(), queue.hook(), member_destroyed);
        (void)source;
    } catch (const std::runtime_error&) { caught = true; }
    CHECK(caught && member_destroyed == 1 && queue.calls == 0);
    allocation.check_balanced();
}

void localization_exception_safety() {
    for (bool null_failure : {false, true}) {
        counting_allocator allocation;
        allocation.fail_at = 0;
        allocation.return_null = null_failure;
        std::atomic<int> destroyed{0};
        auto source = own::make_shared<tracked>(destroyed);
        expect_bad_alloc([&] {
            auto local = source.localize(allocation.ref());
            (void)local;
        });
        CHECK(source && source.use_count() == 1 && destroyed == 0);
        allocation.check_balanced();
        allocation.fail_at = allocation.attempts;
        expect_bad_alloc([&] {
            auto local = std::move(source).localize(allocation.ref());
            (void)local;
        });
        CHECK(source && source.use_count() == 1 && destroyed == 0);
        source.reset();
        CHECK(destroyed == 1);
        allocation.check_balanced();
    }
}

void delayed_retirement() {
    counting_allocator allocation;
    deferred_queue queue;
    std::atomic<int> destroyed{0};
    own::weak_owner<tracked> weak;
    {
        auto source = own::allocate_shared_with<tracked>(allocation.ref(), queue.hook(), destroyed);
        weak = source;
        auto local = source.localize();
        source.reset();
        CHECK(queue.calls == 0 && destroyed == 0);
        local.reset();
    }
    CHECK(queue.calls == 1 && queue.pending.has_value() && destroyed == 0);
    CHECK(weak.expired() && weak.use_count() == 0 && !weak.lock());
    CHECK(!allocation.live.empty());
    queue.pending->run();
    CHECK(destroyed == 1 && !allocation.live.empty());
    queue.pending->run();
    CHECK(destroyed == 1);
    queue.pending.reset();
    weak.reset();
    allocation.check_balanced();
}

void retirement_without_external_weak() {
    counting_allocator allocation;
    deferred_queue queue;
    std::atomic<int> destroyed{0};
    {
        auto source = own::allocate_local_with<tracked>(allocation.ref(), queue.hook(), destroyed);
        auto alias = source;
    }
    CHECK(queue.calls == 1 && destroyed == 0 && !allocation.live.empty());
    queue.pending.reset(); // Cancellation is safe: abandoning a task runs it.
    CHECK(destroyed == 1);
    allocation.check_balanced();
}

void retirement_move_assignment() {
    deferred_queue first;
    deferred_queue second;
    std::atomic<int> first_destroyed{0};
    std::atomic<int> second_destroyed{0};
    own::make_shared_with<tracked>(first.hook(), first_destroyed).reset();
    own::make_local_with<tracked>(second.hook(), second_destroyed).reset();
    CHECK(first.calls == 1 && second.calls == 1);
    *first.pending = std::move(*second.pending);
    CHECK(first_destroyed == 1 && second_destroyed == 0);
    second.pending.reset();
    CHECK(second_destroyed == 0);
    first.pending.reset();
    CHECK(second_destroyed == 1);
}

void retirement_migrates_threads() {
    struct thread_tracked {
        std::thread::id* destructor_thread;
        explicit thread_tracked(std::thread::id& result) : destructor_thread(&result) {}
        ~thread_tracked() { *destructor_thread = std::this_thread::get_id(); }
    };
    deferred_queue queue;
    std::thread::id destructor_thread;
    auto owner = own::make_shared_with<thread_tracked>(queue.hook(), destructor_thread);
    own::weak_owner<thread_tracked> weak = owner;
    owner.reset();
    CHECK(weak.expired());
    std::thread worker([task = std::move(*queue.pending)]() mutable { task.run(); });
    const auto worker_id = worker.get_id();
    worker.join();
    CHECK(destructor_thread == worker_id);
    queue.pending.reset();
}

void inline_retirement_and_null_hook() {
    struct inline_callback {
        static void retire(void* context, own::retirement_task task) noexcept {
            ++*static_cast<int*>(context);
            task.run();
        }
    };
    int calls = 0;
    std::atomic<int> destroyed{0};
    {
        auto first = own::make_shared_with<tracked>({&calls, &inline_callback::retire}, destroyed);
        auto second = own::make_local_with<tracked>({}, destroyed);
    }
    CHECK(calls == 1 && destroyed == 2);
}

void embedded_weak_destruction() {
    struct self_weak {
        own::weak_owner<self_weak> self;
        std::atomic<int>* destroyed;
        bool* saw_expired;
        self_weak(std::atomic<int>& count, bool& expired) : destroyed(&count), saw_expired(&expired) {}
        ~self_weak() {
            *saw_expired = self.expired() && !self.lock();
            ++*destroyed;
        }
    };
    counting_allocator allocation;
    std::atomic<int> destroyed{0};
    bool saw_expired = false;
    {
        auto owner = own::allocate_shared<self_weak>(allocation.ref(), destroyed, saw_expired);
        owner->self = owner;
    }
    CHECK(destroyed == 1 && saw_expired);
    allocation.check_balanced();
    deferred_queue queue;
    saw_expired = false;
    {
        auto owner = own::allocate_shared_with<self_weak>(allocation.ref(), queue.hook(), destroyed, saw_expired);
        owner->self = owner;
    }
    CHECK(destroyed == 1 && queue.calls == 1);
    queue.pending.reset();
    CHECK(destroyed == 2 && saw_expired);
    allocation.check_balanced();
}

void global_concurrency_and_local_fanout() {
    constexpr int worker_count = 6;
    constexpr int iterations = 12000;
    std::atomic<int> destroyed{0};
    std::atomic<int> successes{0};
    auto source = own::make_shared<tracked>(destroyed, 57);
    std::vector<std::thread> workers;
    for (int worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back([source, &successes] {
            auto local = source.localize();
            own::weak_owner<tracked> weak = source;
            for (int iteration = 0; iteration < iterations; ++iteration) {
                auto global_copy = source;
                auto local_copy = local;
                auto locked = weak.lock();
                if (global_copy->value == 57 && local_copy->value == 57 && locked->value == 57)
                    successes.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    source.reset();
    for (auto& worker : workers) worker.join();
    CHECK(successes == worker_count * iterations && destroyed == 1);
}

void weak_race_never_resurrects() {
    constexpr int repetitions = 150;
    constexpr int worker_count = 4;
    for (int repetition = 0; repetition < repetitions; ++repetition) {
        std::atomic<int> destroyed{0};
        auto source = own::make_shared<tracked>(destroyed, 73);
        own::weak_owner<tracked> weak = source;
        std::barrier start(worker_count + 1);
        std::atomic<bool> violation{false};
        std::vector<std::thread> workers;
        for (int worker = 0; worker < worker_count; ++worker) {
            workers.emplace_back([weak, &start, &destroyed, &violation] {
                start.arrive_and_wait();
                for (int attempt = 0; attempt < 200; ++attempt) {
                    auto locked = weak.lock();
                    if (locked && (locked->value != 73 || destroyed.load() != 0)) violation = true;
                }
            });
        }
        start.arrive_and_wait();
        source.reset();
        for (auto& worker : workers) worker.join();
        CHECK(!violation && destroyed == 1);
        CHECK(weak.expired() && !weak.lock());
    }
}

void weak_copy_drop_concurrency() {
    counting_allocator allocation;
    std::atomic<int> destroyed{0};
    auto source = own::allocate_shared<tracked>(allocation.ref(), destroyed);
    own::weak_owner<tracked> weak = source;
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 6; ++worker) {
        workers.emplace_back([weak] {
            for (int iteration = 0; iteration < 20000; ++iteration) {
                auto copy = weak;
                auto moved = std::move(copy);
                copy = moved;
                auto locked = moved.lock();
            }
        });
    }
    source.reset();
    weak.reset();
    for (auto& worker : workers) worker.join();
    CHECK(destroyed == 1);
    allocation.check_balanced();
}

void deferred_final_release_race() {
    counting_allocator allocation;
    deferred_queue queue;
    std::atomic<int> destroyed{0};
    auto source = own::allocate_shared_with<tracked>(allocation.ref(), queue.hook(), destroyed);
    own::weak_owner<tracked> weak = source;
    std::barrier start(9);
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 8; ++worker) {
        workers.emplace_back([owner = source, &start]() mutable {
            start.arrive_and_wait();
            owner.reset();
        });
    }
    source.reset();
    start.arrive_and_wait();
    for (auto& worker : workers) worker.join();
    CHECK(queue.calls == 1 && destroyed == 0 && weak.expired());
    weak.reset();
    CHECK(!allocation.live.empty());
    queue.pending.reset();
    CHECK(destroyed == 1);
    allocation.check_balanced();
}
} // namespace

int main() {
    test::run("empty owners", empty_owners);
    test::run("shared lifetime", shared_lifetime);
    test::run("local lifetime and promotion", local_lifetime_and_promotion);
    test::run("independent local groups", independent_local_groups);
    test::run("rvalue transitions", rvalue_transitions);
    test::run("weak semantics", weak_semantics);
    test::run("shared conversions and pointer adjustment", shared_conversion_adjusts_pointer);
    test::run("local conversions and pointer adjustment", local_conversion_adjusts_pointer);
    test::run("immutable and move-only payloads", immutable_and_move_only_payloads);
    test::run("class-specific operator new bypass", class_specific_new_is_bypassed);
    test::run("virtual-base weak conversions", virtual_base_weak_conversions);
    test::run("virtual-base weak conversion race", virtual_base_weak_conversion_race);
    test::run("custom allocator lifetime", custom_allocator_lifetime);
    test::run("over-aligned storage", over_aligned_storage);
    test::run("throwing constructors", throwing_constructors);
    test::run("initial allocation failures", initial_allocation_failures);
    test::run("local group allocation failures", local_group_allocation_failures);
    test::run("failed factory does not retire", failed_factory_does_not_retire);
    test::run("localization exception safety", localization_exception_safety);
    test::run("delayed retirement", delayed_retirement);
    test::run("retirement without external weak", retirement_without_external_weak);
    test::run("retirement move assignment", retirement_move_assignment);
    test::run("retirement thread migration", retirement_migrates_threads);
    test::run("inline retirement and null hook", inline_retirement_and_null_hook);
    test::run("embedded weak destruction", embedded_weak_destruction);
    test::run("global concurrency and local fanout", global_concurrency_and_local_fanout);
    test::run("weak race never resurrects", weak_race_never_resurrects);
    test::run("weak copy/drop concurrency", weak_copy_drop_concurrency);
    test::run("deferred final-release race", deferred_final_release_race);
    return test::finish();
}

// A group created by a local factory skips the atomic release while its strong
// reference is the only reference of any kind. Every way to create another
// reference must end that state first; these cases release in each order.
#include <own/ownership.hpp>

#include "test_support.hpp"

#include <atomic>
#include <new>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace {
struct tracked {
    std::atomic<int>* destroyed;
    int value = 7;
    explicit tracked(std::atomic<int>& count) : destroyed(&count) {}
    ~tracked() { destroyed->fetch_add(1); }
};
struct self_tracked : own::enable_owner_from_this<self_tracked> {
    std::atomic<int>* destroyed;
    explicit self_tracked(std::atomic<int>& count) : destroyed(&count) {}
    ~self_tracked() { destroyed->fetch_add(1); }
};

struct counting_allocator {
    int allocated = 0, deallocated = 0;
    static void* allocate(void* context, std::size_t bytes, std::size_t alignment) {
        ++static_cast<counting_allocator*>(context)->allocated;
        return ::operator new(bytes, std::align_val_t(alignment));
    }
    static void deallocate(void* context, void* pointer, std::size_t, std::size_t alignment) noexcept {
        ++static_cast<counting_allocator*>(context)->deallocated;
        ::operator delete(pointer, std::align_val_t(alignment));
    }
    own::allocator_ref ref() { return {this, &allocate, &deallocate}; }
};

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
} // namespace

int main() {
    test::run("exclusive local owner destroys once and frees its block", [] {
        counting_allocator allocation;
        std::atomic<int> destroyed{0};
        {
            auto owner = own::allocate_local<tracked>(allocation.ref(), destroyed);
            auto copy = owner;
            own::local_owner<const tracked> constant = copy;
            CHECK(owner.local_use_count() == 3 && owner.use_count() == 1 && constant->value == 7);
        }
        CHECK(destroyed == 1 && allocation.allocated == 1 && allocation.deallocated == 1);
    });

    test::run("share ends exclusivity in either release order", [] {
        for (bool local_first : {true, false}) {
            counting_allocator allocation;
            std::atomic<int> destroyed{0};
            auto local = own::allocate_local<tracked>(allocation.ref(), destroyed);
            auto shared = local.share();
            CHECK(local.use_count() == 2 && local.local_use_count() == 1);
            if (local_first) { local.reset(); } else { shared.reset(); }
            CHECK(destroyed == 0 && allocation.deallocated == 0);
            if (local_first) {
                CHECK(shared.use_count() == 1);
                shared.reset();
            } else {
                CHECK(local.use_count() == 1);
                local.reset();
            }
            CHECK(destroyed == 1 && allocation.deallocated == 1);
        }
    });

    test::run("shared copy returned to a temporary still ends exclusivity", [] {
        std::atomic<int> destroyed{0};
        auto local = own::make_local<tracked>(destroyed);
        (void)local.share(); // the share is dropped at once; state stays shared
        auto copy = local;
        copy.reset();
        CHECK(destroyed == 0 && local.use_count() == 1);
        local.reset();
        CHECK(destroyed == 1);
    });

    test::run("weak observation ends exclusivity", [] {
        counting_allocator allocation;
        std::atomic<int> destroyed{0};
        own::weak_owner<tracked> weak;
        {
            auto local = own::allocate_local<tracked>(allocation.ref(), destroyed);
            weak = local;
            auto locked = weak.lock();
            CHECK(locked && locked.use_count() == 2);
        }
        CHECK(destroyed == 1 && weak.expired() && !weak.lock());
        CHECK(allocation.deallocated == 0); // the weak observer keeps the block
        weak.reset();
        CHECK(allocation.deallocated == 1);
    });

    test::run("registered payload is never exclusive", [] {
        std::atomic<int> destroyed{0};
        auto local = own::make_local<self_tracked>(destroyed);
        // Another thread turns a borrowed view into ownership through the
        // registration, without going through the group.
        own::shared_owner<self_tracked> escaped;
        std::thread worker([view = local.borrow(), &escaped] { escaped = view->shared_from_this(); });
        worker.join();
        CHECK(local.use_count() == 2);
        local.reset();
        CHECK(destroyed == 0 && escaped.use_count() == 1);
        escaped.reset();
        CHECK(destroyed == 1);
    });

    test::run("exclusive release still runs the retirement hook", [] {
        counting_allocator allocation;
        deferred_queue queue;
        std::atomic<int> destroyed{0};
        {
            auto local = own::allocate_local_with<tracked>(allocation.ref(), queue.hook(), destroyed);
            auto copy = local;
        }
        CHECK(queue.calls == 1 && queue.pending && destroyed == 0 && allocation.deallocated == 0);
        queue.pending.reset();
        CHECK(destroyed == 1 && allocation.deallocated == 1);
    });

    test::run("shared handed to other threads after leaving exclusivity", [] {
        for (int round = 0; round < 200; ++round) {
            std::atomic<int> destroyed{0};
            auto local = own::make_local<tracked>(destroyed);
            auto shared = local.share();
            std::vector<std::thread> workers;
            for (int t = 0; t < 3; ++t) {
                workers.emplace_back([shared] {
                    for (int i = 0; i < 100; ++i) { auto copy = shared; (void)copy->value; }
                });
            }
            shared.reset();
            local.reset();
            for (auto& worker : workers) worker.join();
            CHECK(destroyed == 1);
        }
    });

    return test::finish();
}

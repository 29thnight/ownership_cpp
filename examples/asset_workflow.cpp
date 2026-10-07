#include <own/ownership.hpp>
#include <array>
#include <cassert>
#include <cstdio>
#include <mutex>
#include <thread>
#include <utility>

namespace
{
    // Fixed storage gives this example a no-allocation enqueue path. A real
    // renderer also needs per-entry fence values and a robust queue-full policy.
    class retirement_queue
    {
    public:
        static void retire(void* context, own::retirement_task task) noexcept
        {
            auto& queue = *static_cast<retirement_queue*>(context);
            std::lock_guard lock(queue.mutex_);
            if (queue.size_ == queue.tasks_.size()) { std::abort(); }
            queue.tasks_[queue.size_++] = std::move(task);
        }
        void drain_after_fence()
        {
            std::array<own::retirement_task, 32> ready;
            {
                std::lock_guard lock(mutex_);
                ready.swap(tasks_);
                size_ = 0;
            }
            // Destruction occurs outside the lock, on the calling render thread.
            for (auto& task : ready) { task.run(); }
        }
    private:
        std::mutex mutex_;
        std::array<own::retirement_task, 32> tasks_;
        std::size_t size_ = 0;
    };

    struct asset
    {
        int version;
        int* destroyed;
        asset(int v, int* counter) noexcept : version(v), destroyed(counter) {}
        ~asset() noexcept { ++*destroyed; }
    };
}

int main()
{
    int destroyed = 0;
    retirement_queue queue;
    own::retirement_hook hook{&queue, retirement_queue::retire};
    auto cache = own::make_shared_with<asset>(hook, 1, &destroyed);
    own::weak_owner<const asset> observed(cache);
    auto scene = cache; // scene owns; component merely borrows
    auto component = scene.borrow();

    std::thread worker([lease = scene]() mutable
    {
        auto local = lease.borrow(); // lease owns the whole worker scope
        auto operation = local;
        assert(operation->version == 1);
    });
    cache = own::make_shared_with<asset>(hook, 2, &destroyed); // hot reload
    assert(component->version == 1); // old scene still has old data
    component.reset(); // discard borrowed access before its lifetime ends
    scene.reset();
    worker.join();
    assert(observed.expired());
    assert(!observed.lock());
    assert(destroyed == 0); // CPU ownership gone, simulated GPU still pending
    queue.drain_after_fence(); // In an engine: only after actual fence completion
    assert(destroyed == 1);
    cache.reset();
    queue.drain_after_fence();
    assert(destroyed == 2);
    std::puts("asset workflow passed (CPU-only fence simulation)");
}

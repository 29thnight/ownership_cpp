// The handle a factory returns carries a hint that it may hold the sole
// reference; only it checks the counts before releasing. These cases release a
// hinted handle in every relation to copies, observers, conversions and other
// threads; the check, not the hint, must decide.
#include <own/ownership.hpp>

#include "test_support.hpp"

#include <atomic>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace {
struct tracked {
    std::atomic<int>* destroyed;
    int value = 5;
    explicit tracked(std::atomic<int>& count) : destroyed(&count) {}
    virtual ~tracked() { destroyed->fetch_add(1); }
};
struct derived : tracked { using tracked::tracked; };

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
    test::run("hinted handle moved and released alone", [] {
        std::atomic<int> destroyed{0};
        {
            std::vector<own::shared_owner<tracked>> owners;
            owners.push_back(own::make_shared<tracked>(destroyed));
            own::shared_owner<tracked> moved = std::move(owners.back());
            CHECK(moved.use_count() == 1 && moved->value == 5 && !owners.back());
        }
        CHECK(destroyed == 1);
    });

    test::run("hinted handle released before and after its copies", [] {
        for (bool hinted_first : {true, false}) {
            std::atomic<int> destroyed{0};
            auto hinted = own::make_shared<tracked>(destroyed);
            auto copy = hinted;
            CHECK(hinted.use_count() == 2 && copy.use_count() == 2);
            if (hinted_first) { hinted.reset(); } else { copy.reset(); }
            CHECK(destroyed == 0);
            if (hinted_first) { CHECK(copy.use_count() == 1); copy.reset(); }
            else { CHECK(hinted.use_count() == 1); hinted.reset(); }
            CHECK(destroyed == 1);
        }
    });

    test::run("assignment from a hinted handle copies without the hint", [] {
        std::atomic<int> destroyed{0};
        own::shared_owner<tracked> target;
        {
            auto hinted = own::make_shared<tracked>(destroyed);
            target = hinted;
            own::shared_owner<const tracked> converted = hinted; // converting copy
            CHECK(target.use_count() == 3 && converted->value == 5);
        }
        CHECK(destroyed == 0 && target.use_count() == 1);
        target.reset();
        CHECK(destroyed == 1);
    });

    test::run("converting move keeps the handle releasable", [] {
        std::atomic<int> destroyed{0};
        {
            own::shared_owner<tracked> base = own::make_shared<derived>(destroyed);
            own::shared_owner<const tracked> constant = std::move(base);
            CHECK(!base && constant.use_count() == 1);
        }
        CHECK(destroyed == 1);
    });

    test::run("weak observers keep a hinted release on the shared path", [] {
        std::atomic<int> destroyed{0};
        auto hinted = own::make_shared<tracked>(destroyed);
        own::weak_owner<tracked> weak = hinted;
        hinted.reset();
        CHECK(destroyed == 1 && weak.expired() && !weak.lock());
        weak.reset();
    });

    test::run("weak lock racing a hinted release never resurrects", [] {
        for (int round = 0; round < 500; ++round) {
            std::atomic<int> destroyed{0};
            auto hinted = own::make_shared<tracked>(destroyed);
            own::weak_owner<tracked> weak = hinted;
            std::atomic<bool> go{false};
            std::thread locker([&] {
                while (!go.load()) {}
                if (auto locked = weak.lock()) { CHECK(locked->value == 5 && destroyed == 0); }
            });
            go = true;
            hinted.reset();
            locker.join();
            CHECK(destroyed == 1);
        }
    });

    test::run("copies dropped on other threads while the hinted handle drops", [] {
        for (int round = 0; round < 500; ++round) {
            std::atomic<int> destroyed{0};
            auto hinted = own::make_shared<tracked>(destroyed);
            std::vector<std::thread> workers;
            for (int t = 0; t < 3; ++t) {
                workers.emplace_back([copy = hinted]() mutable {
                    for (int i = 0; i < 20; ++i) { auto again = copy; (void)again->value; }
                });
            }
            hinted.reset();
            for (auto& worker : workers) worker.join();
            CHECK(destroyed == 1);
        }
    });

    test::run("hinted release runs the retirement hook once", [] {
        deferred_queue queue;
        std::atomic<int> destroyed{0};
        { auto hinted = own::make_shared_with<tracked>(queue.hook(), destroyed); }
        CHECK(queue.calls == 1 && queue.pending && destroyed == 0);
        queue.pending.reset();
        CHECK(destroyed == 1);
    });

    test::run("hinted handle localizes and observes correctly", [] {
        std::atomic<int> destroyed{0};
        auto hinted = own::make_shared<tracked>(destroyed);
        auto local = hinted.localize();
        own::weak_owner<tracked> weak = hinted;
        CHECK(hinted.use_count() == 2 && weak.use_count() == 2);
        hinted.reset();
        CHECK(destroyed == 0 && local.use_count() == 1);
        local.reset();
        CHECK(destroyed == 1 && weak.expired());
    });

    return test::finish();
}

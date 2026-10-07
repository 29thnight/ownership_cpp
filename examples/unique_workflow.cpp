#include <own/ownership.hpp>
#include <cassert>
#include <cstdio>
#include <thread>
#include <utility>

namespace
{
    struct resource
    {
        int version;
        int* destroyed;
        resource(int v, int& count) noexcept : version(v), destroyed(&count) {}
        ~resource() noexcept { ++*destroyed; }
    };

    int inspect(own::local_view<const resource> view) noexcept
    {
        return view->version;
    }
}

int main()
{
    int destroyed = 0;
    auto owner = own::make_unique<resource>(7, destroyed);
    assert(inspect(owner.borrow()) == 7);
    std::thread job([lease = std::move(owner)]
    {
        // The task is now the exclusive owner. Borrow only while it owns.
        assert(inspect(lease.borrow()) == 7);
    });
    assert(!owner);
    job.join();
    assert(destroyed == 1);
    std::puts("exclusive ownership handoff and scoped borrowing passed");
}

#include <own/ownership.hpp>

#include "test_support.hpp"

#include <csignal>
#include <cstdio>
#include <functional>
#include <thread>
#include <utility>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
#if defined(__unix__) || defined(__APPLE__)
void expect_abort(const std::function<void()>& body) {
    std::cout.flush();
    std::cerr.flush();
    const pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        rlimit no_core{0, 0};
        setrlimit(RLIMIT_CORE, &no_core);
        (void)std::freopen("/dev/null", "w", stderr);
        body();
        _exit(0);
    }
    int status = 0;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
}

template <class F> void foreign_thread(F&& operation) {
    expect_abort([&] {
        auto local = own::make_local<int>(19);
        std::thread worker([&] { operation(local); });
        worker.join();
    });
}
#endif
} // namespace

int main() {
#if defined(NDEBUG)
    std::cerr << "Thread-confinement death tests require assertions enabled\n";
    return EXIT_FAILURE;
#elif defined(__unix__) || defined(__APPLE__)
    test::run("foreign unsafe_get", [] { foreign_thread([](auto& x) { (void)x.unsafe_get(); }); });
    test::run("foreign borrow", [] { foreign_thread([](auto& x) { (void)x.borrow(); }); });
    test::run("foreign bool", [] { foreign_thread([](auto& x) { (void)static_cast<bool>(x); }); });
    test::run("foreign dereference", [] { foreign_thread([](auto& x) { (void)*x; }); });
    test::run("foreign arrow", [] { foreign_thread([](auto& x) { (void)x.operator->(); }); });
    test::run("foreign shared count", [] { foreign_thread([](auto& x) { (void)x.use_count(); }); });
    test::run("foreign local count", [] { foreign_thread([](auto& x) { (void)x.local_use_count(); }); });
    test::run("foreign copy", [] { foreign_thread([](auto& x) { auto copied = x; (void)copied; }); });
    test::run("foreign move", [] { foreign_thread([](auto& x) { auto moved = std::move(x); (void)moved; }); });
    test::run("foreign reset", [] { foreign_thread([](auto& x) { x.reset(); }); });
    test::run("foreign share", [] { foreign_thread([](auto& x) { (void)x.share(); }); });
    test::run("foreign weak construction", [] {
        foreign_thread([](auto& x) { own::weak_owner<int> weak = x; (void)weak; });
    });
    test::run("foreign assignment source", [] {
        foreign_thread([](auto& x) { own::local_owner<int> target; target = x; });
    });
    test::run("foreign assignment target", [] {
        foreign_thread([](auto& x) { x = own::make_local<int>(7); });
    });
    test::run("foreign swap", [] {
        foreign_thread([](auto& x) { auto other = own::make_local<int>(7); other.swap(x); });
    });
    test::run("foreign destruction", [] {
        expect_abort([] {
            auto* local = new own::local_owner<int>(own::make_local<int>(19));
            std::thread worker([local] { delete local; });
            worker.join();
        });
    });
    // White-box tests for unreachable-in-practice overflow states. These only
    // exercise counter helpers; real handles never have their state corrupted.
    test::run("atomic reference overflow", [] {
        expect_abort([] {
            std::atomic<std::size_t> count{own::detail::count_limit};
            own::detail::increment(count);
        });
    });
    test::run("atomic reference saturation", [] {
        // The last value below the threshold still increments normally.
        std::atomic<std::size_t> below{own::detail::saturation_limit - 1};
        own::detail::increment(below);
        CHECK(below.load() == own::detail::saturation_limit);
        expect_abort([] {
            std::atomic<std::size_t> count{own::detail::saturation_limit};
            own::detail::increment(count);
        });
    });
    test::run("weak locking saturation", [] {
        expect_abort([] {
            own::detail::control_block block(nullptr);
            block.strong.store(own::detail::saturation_limit);
            (void)own::detail::try_add_strong(&block);
        });
    });
    test::run("atomic zero cannot resurrect", [] {
        expect_abort([] {
            std::atomic<std::size_t> count{0};
            own::detail::increment(count);
        });
    });
    test::run("weak locking overflow", [] {
        expect_abort([] {
            own::detail::control_block block(nullptr);
            block.strong.store(own::detail::count_limit);
            (void)own::detail::try_add_strong(&block);
        });
    });
    test::run("local reference overflow", [] {
        expect_abort([] {
            own::detail::local_group group(nullptr, {});
            group.references = own::detail::count_limit;
            group.add_reference();
        });
    });
    test::run("invalid allocator callbacks", [] {
        expect_abort([] {
            auto invalid = own::allocate_shared<int>({nullptr, nullptr, nullptr}, 3);
            (void)invalid;
        });
    });
    return test::finish();
#else
    std::cout << "SKIP thread-confinement death tests: POSIX fork unavailable\n";
    return EXIT_SUCCESS;
#endif
}

// OWN_FAIL_FAST receives the reason of a detected contract violation.
// Each scenario runs in its own process: `fail_hook_tests <scenario>`. The hook
// prints the reason and exits with 3, so the runner can tell a reported
// failure apart from a plain abort (which would mean the hook was bypassed).
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace own { enum class failure : unsigned char; }
[[noreturn]] void report_and_exit(own::failure reason) noexcept;
#define OWN_FAIL_FAST(reason) ::report_and_exit(reason)
#define OWN_DEBUG_THREAD_CHECK 1

#include <own/ownership.hpp>

#include <thread>

[[noreturn]] void report_and_exit(own::failure reason) noexcept {
    std::printf("reported %d: %s\n", static_cast<int>(reason), own::to_string(reason));
    std::fflush(stdout);
    std::_Exit(3);
}

int main(int argc, char** argv) {
    const char* scenario = argc > 1 ? argv[1] : "";
    if (std::strcmp(scenario, "invalid_allocator") == 0) {
        own::allocator_ref broken{nullptr, nullptr, own::allocator_ref{}.deallocate};
        auto owner = own::allocate_shared<int>(broken, 1);
        (void)owner;
    } else if (std::strcmp(scenario, "thread_mismatch") == 0) {
        auto owner = own::make_local<int>(1);
        std::thread([&] { auto copy = owner; (void)copy; }).join();
    } else if (std::strcmp(scenario, "none") == 0) {
        auto owner = own::make_local<int>(1);
        auto copy = owner;
        std::printf("no failure: %d\n", *copy);
        return 0;
    } else {
        std::fprintf(stderr, "usage: fail_hook_tests invalid_allocator|thread_mismatch|none\n");
        return 2;
    }
    std::printf("scenario did not fail\n");
    return 1;
}

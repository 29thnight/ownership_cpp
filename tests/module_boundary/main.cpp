#include "boundary.hpp"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <thread>
#if defined(_WIN32)
#include <crtdbg.h>
#endif

extern "C" void report_abort(int) {
    // The debug thread check aborted. Say so and exit normally, so a test
    // runner can tell this documented outcome apart from any other crash.
    std::fputs("ABORTED by the local_owner thread check\n", stdout);
    std::fflush(stdout);
    std::_Exit(4);
}

int main() {
#if defined(_WIN32)
    // The per-binary configuration aborts by design; report it without a dialog.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    std::signal(SIGABRT, report_abort);
    // This binary hands out its first thread ID to the main thread.
    auto here = own::make_local<int>(1);
    int result = 1;
    std::thread worker([&] {
        auto warm = own::make_local<int>(2);  // this binary: second ID
        auto from_module = make_in_module(3); // the module: its counter's first ID
        auto copy = from_module;              // checked here, on the same thread
        result = *copy == 3 && from_module.local_use_count() == 2 ? 0 : 1;
    });
    worker.join();
    std::printf(result == 0 ? "PASS local_owner crosses a module boundary on one thread\n"
                            : "FAIL module boundary copy\n");
    return result;
}

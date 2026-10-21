#pragma once

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#include <crtdbg.h>
#include <stdlib.h>
#endif

namespace test {
#if defined(_WIN32)
// An unexpected abort or CRT assertion must fail the test, not wait on a
// dialog box that no one on a CI runner will close.
inline const bool no_error_dialogs = [] {
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    return true;
}();
#endif
inline void check(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        throw std::runtime_error(std::string(file) + ":" + std::to_string(line) +
                                 ": check failed: " + expression);
    }
}
inline int failures = 0;
inline int cases = 0;
template <class F> void run(const char* name, F&& body) {
    ++cases;
    try {
        body();
        std::cout << "PASS " << name << '\n';
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "FAIL " << name << ": " << error.what() << '\n';
    } catch (...) {
        ++failures;
        std::cerr << "FAIL " << name << ": unknown exception\n";
    }
}
inline int finish() {
    std::cout << cases - failures << '/' << cases << " tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
} // namespace test

#define CHECK(...) ::test::check(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__, __FILE__, __LINE__)

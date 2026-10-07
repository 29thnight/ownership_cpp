#pragma once

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace test {
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

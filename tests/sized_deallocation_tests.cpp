// Default-allocator blocks are returned with their exact size when the
// implementation provides sized deallocation (__cpp_sized_deallocation).
#include <cstdio>
#include <cstdlib>
#include <new>

namespace {
std::size_t last_sized = 0;
int sized_calls = 0;
int unsized_calls = 0;
} // namespace

// Dedicated executable: replaces global allocation to observe the size.
void* operator new(std::size_t size) {
    if (void* result = std::malloc(size == 0 ? 1 : size)) return result;
    throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept { ++unsized_calls; std::free(pointer); }
void operator delete(void* pointer, std::size_t size) noexcept {
    ++sized_calls;
    last_sized = size;
    std::free(pointer);
}

#include <own/ownership.hpp>

#include "test_support.hpp"

namespace {
struct payload { char bytes[40]; };

void blocks_report_their_size() {
#if defined(__cpp_sized_deallocation)
    sized_calls = unsized_calls = 0;
    { auto shared = own::make_shared<payload>(); }
    CHECK(sized_calls == 1 && unsized_calls == 0);
    CHECK(last_sized == sizeof(own::detail::in_place_control<payload>));
    { auto local = own::make_local<payload>(); }
    CHECK(last_sized == sizeof(own::detail::in_place_control<payload, true>));
    { auto shared = own::make_shared<payload>(); auto local = shared.localize(); }
    CHECK(unsized_calls == 0);
#else
    std::puts("SKIP sized deallocation: not enabled for this compiler (e.g. Clang < 19 without -fsized-deallocation)");
#endif
}
} // namespace

int main() {
    test::run("blocks report their size", blocks_report_their_size);
    return test::finish();
}

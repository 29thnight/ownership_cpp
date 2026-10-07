#include <own/ownership.hpp>

#include <type_traits>
#include <utility>

// The runner requires this source to fail with the exact deprecation message
// by default and with =1, and to compile/run with =0. It is never globally
// silenced with -Wno-deprecated-declarations.
#ifndef OWN_UNSAFE_GET_KIND
#define OWN_UNSAFE_GET_KIND 0
#endif

int main() {
#if OWN_UNSAFE_GET_KIND == 0
    const auto owner = own::make_shared<int>(17);
    return *owner.unsafe_get() == 17 ? 0 : 1;
#elif OWN_UNSAFE_GET_KIND == 1
    const auto owner = own::make_local<int>(17);
    return *owner.unsafe_get() == 17 ? 0 : 1;
#elif OWN_UNSAFE_GET_KIND == 2
    const auto owner = own::make_shared<int>(17);
    const auto borrowed = owner.borrow();
    return *borrowed.unsafe_get() == 17 ? 0 : 1;
#elif OWN_UNSAFE_GET_KIND == 3
    const auto owner = own::make_shared<int>(17);
    // Temporary views are allowed: this is not a temporary owner.
    return *owner.borrow().unsafe_get() == 17 ? 0 : 1;
#else
#error Unsupported OWN_UNSAFE_GET_KIND
#endif
}

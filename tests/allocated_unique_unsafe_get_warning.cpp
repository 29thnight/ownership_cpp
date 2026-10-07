#include <own/ownership.hpp>

// Required to fail under -Werror with the exact deprecation diagnostic by
// default and with =1, and to compile/run with the warning setting =0.
int main() {
    const auto owner = own::allocate_unique<int>({}, 17);
    return *owner.unsafe_get() == 17 ? 0 : 1;
}

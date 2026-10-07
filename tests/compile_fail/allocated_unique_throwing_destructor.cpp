#include <own/ownership.hpp>
#include <memory>
#include <utility>

struct value { ~value() noexcept(false) {} };
int main() { auto owner = own::allocate_unique<value>({}); }

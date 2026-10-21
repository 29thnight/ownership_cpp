// expect: static assert
// expect: owned\ destructors\ must\ be\ accessible\ and\ noexcept
#include <own/ownership.hpp>
#include <memory>
#include <utility>

struct value { ~value() noexcept(false) {} };
int main() { auto owner = own::make_unique<value>(); }

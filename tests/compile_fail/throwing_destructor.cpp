// expect: static assert
// expect: owned\ destructors\ must\ be\ noexcept
#include <own/ownership.hpp>
struct unsafe { ~unsafe() noexcept(false) {} };
int main() { auto invalid = own::make_shared<unsafe>(); }

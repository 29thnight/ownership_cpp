// expect: (no matching function for call to '(own::)?unique_owner<int>::reset\(int\*\)'|too many arguments to function call)
// expect: reset
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto owner = own::make_unique<int>(1); owner.reset(new int(2)); }

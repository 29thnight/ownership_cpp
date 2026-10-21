// expect: (no matching function for call to '(own::)?shared_owner<int>::reset\(int\*\)'|too many arguments to function call)
// expect: reset
#include <own/ownership.hpp>
int main() { int value = 1; own::shared_owner<int> owner; owner.reset(&value); }

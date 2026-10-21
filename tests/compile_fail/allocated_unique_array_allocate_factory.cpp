// expect: static assert
// expect: is_array
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto owner = own::allocate_unique<int[]>({}); }

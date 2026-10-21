// expect: static assert
// expect: is_array
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { own::unique_owner<int[3], own::allocator_ref> owner; }

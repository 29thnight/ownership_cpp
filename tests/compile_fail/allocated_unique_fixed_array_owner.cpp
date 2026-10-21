// expect: static assert
// expect: is_array
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { own::allocated_unique_owner<int[3]> owner; }

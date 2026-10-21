// expect: static assert
// expect: unique_owner's second argument must be own::new_delete, own::allocator_ref, or an
#include <own/ownership.hpp>
#include <memory>

int main() { own::unique_owner<int, std::allocator<int>> owner; }

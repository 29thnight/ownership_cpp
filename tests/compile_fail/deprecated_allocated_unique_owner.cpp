// only: gcc
// expect: allocated_unique_owner.*deprecated
// expect: use own::unique_owner<T, own::allocator_ref>
#include <own/ownership.hpp>

int main() { own::allocated_unique_owner<int> owner; return owner ? 1 : 0; }

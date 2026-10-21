// expect: deleted.*operator ?'?=
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto owner = own::allocate_unique<int>({}, 1); own::unique_owner<int, own::allocator_ref> copy; copy = owner; }

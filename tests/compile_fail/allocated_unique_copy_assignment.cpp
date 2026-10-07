#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto owner = own::allocate_unique<int>({}, 1); own::allocated_unique_owner<int> copy; copy = owner; }

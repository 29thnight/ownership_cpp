#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto source = own::allocate_unique<const int>({}, 1); own::allocated_unique_owner<int> owner; owner = std::move(source); }

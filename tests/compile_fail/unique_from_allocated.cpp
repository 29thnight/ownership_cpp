#include <own/ownership.hpp>
#include <utility>

int main() { auto source = own::allocate_unique<int>({}, 1); own::unique_owner<int> owner(std::move(source)); }

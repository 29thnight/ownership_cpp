#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto source = own::make_local<int>(1); own::allocated_unique_owner<int> owner(std::move(source)); }

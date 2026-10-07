#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto source = own::make_shared<int>(1); own::unique_owner<int> owner(std::move(source)); }

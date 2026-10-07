#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto source = own::make_unique<int>(1); own::weak_owner<int> owner(source); }

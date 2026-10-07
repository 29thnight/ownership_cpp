#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto owner = own::make_unique<int>(1); own::unique_owner<int> copy; copy = owner; }

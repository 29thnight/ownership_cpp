// expect: no matching function for call to '?allocate_unique
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto owner = own::allocate_unique<int>(std::allocator<int>{}, 3); }

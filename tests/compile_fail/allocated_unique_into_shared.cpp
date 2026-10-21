// expect: no matching (function for call to|constructor for initialization of) '(own::)?shared_owner<int>
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto source = own::allocate_unique<int>({}, 1); own::shared_owner<int> owner(std::move(source)); }

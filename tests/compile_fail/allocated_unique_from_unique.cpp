// expect: no matching (function for call to|constructor for initialization of) '(own::)?allocated_unique_owner<int>
#include <own/ownership.hpp>
#include <utility>

int main() { auto source = own::make_unique<int>(1); own::allocated_unique_owner<int> owner(std::move(source)); }

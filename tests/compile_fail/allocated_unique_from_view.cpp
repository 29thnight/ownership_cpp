// expect: no matching (function for call to|constructor for initialization of) '(own::)?allocated_unique_owner<int>
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto source = own::make_shared<int>(1); own::allocated_unique_owner<int> owner(source.borrow()); }

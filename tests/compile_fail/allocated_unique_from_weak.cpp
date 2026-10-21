// expect: no matching (function for call to|constructor for initialization of) '(own::)?allocated_unique_owner<int>
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { own::weak_owner<int> source; own::allocated_unique_owner<int> owner(std::move(source)); }

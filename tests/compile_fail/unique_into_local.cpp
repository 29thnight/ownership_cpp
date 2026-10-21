// expect: no matching (function for call to|constructor for initialization of) '(own::)?local_owner<int>
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto source = own::make_unique<int>(1); own::local_owner<int> owner(std::move(source)); }

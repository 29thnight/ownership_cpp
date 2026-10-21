// expect: no matching (function for call to|constructor for initialization of) '(own::)?allocated_unique_owner<int>
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { int* value = nullptr; own::allocated_unique_owner<int> owner(value, [](int* pointer) { delete pointer; }); }

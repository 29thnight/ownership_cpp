// expect: deleted (function '(own::)?allocated_unique_owner<T>::allocated_unique_owner\(const|constructor of '(own::)?allocated_unique_owner<int>')
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto owner = own::allocate_unique<int>({}, 1); auto copy = owner; }

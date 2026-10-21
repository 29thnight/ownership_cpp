// expect: deleted (function '(own::)?unique_owner<T>::unique_owner\(const|constructor of '(own::)?unique_owner<int>')
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto owner = own::make_unique<int>(1); auto copy = owner; }

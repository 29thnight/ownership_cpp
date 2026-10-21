// expect: (cannot convert '(own::)?allocated_unique_owner<int>' to 'int\*'|no viable conversion from '(own::)?allocated_unique_owner<int>' to 'int \*')
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto owner = own::allocate_unique<int>({}, 1); int* raw = owner; }

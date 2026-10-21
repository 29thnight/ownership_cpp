// expect: (cannot convert '(own::)?unique_owner<int>' to 'int\*'|no viable conversion from '(own::)?unique_owner<int>' to 'int \*')
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto owner = own::make_unique<int>(1); int* raw = owner; }

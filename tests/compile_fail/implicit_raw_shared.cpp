// expect: (cannot convert '(own::)?shared_owner<int>' to 'int\*'|no viable conversion from '(own::)?shared_owner<int>' to 'int \*')
#include <own/ownership.hpp>
int main() { auto owner = own::make_shared<int>(1); int* invalid = owner; (void)invalid; }

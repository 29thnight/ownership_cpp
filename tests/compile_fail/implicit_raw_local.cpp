// expect: (cannot convert '(own::)?local_owner<int>' to 'int\*'|no viable conversion from '(own::)?local_owner<int>' to 'int \*')
#include <own/ownership.hpp>
int main() { auto owner = own::make_local<int>(1); int* invalid = owner; (void)invalid; }

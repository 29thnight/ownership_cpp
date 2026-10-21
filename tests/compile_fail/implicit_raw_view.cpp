// expect: (cannot convert '(own::)?borrowed<int>' to 'int\*'|no viable conversion from '(own::)?borrowed<int>' to 'int \*')
#include <own/ownership.hpp>
int main() { auto owner = own::make_shared<int>(1); int* invalid = owner.borrow(); (void)invalid; }

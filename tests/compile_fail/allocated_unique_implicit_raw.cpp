// expect: (cannot convert '(own::)?unique_owner<int, ?(\[\.\.\.\]|(own::)?allocator_ref)>' to 'int\*'|no viable conversion from '(own::)?unique_owner<int, ?(\[\.\.\.\]|(own::)?allocator_ref)>' to 'int \*')
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto owner = own::allocate_unique<int>({}, 1); int* raw = owner; }

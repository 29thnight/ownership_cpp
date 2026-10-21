// expect: deleted (function '(own::)?unique_owner<T, (own::)?allocator_ref>::unique_owner\(const|constructor of '(own::)?unique_owner<int, ?(\[\.\.\.\]|(own::)?allocator_ref)>')
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto owner = own::allocate_unique<int>({}, 1); auto copy = owner; }

// expect: (no match for 'operator=' \(operand types are '(own::)?unique_owner<int, ?(\[\.\.\.\]|(own::)?allocator_ref)>'|no viable overloaded '=')
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto source = own::allocate_unique<const int>({}, 1); own::unique_owner<int, own::allocator_ref> owner; owner = std::move(source); }

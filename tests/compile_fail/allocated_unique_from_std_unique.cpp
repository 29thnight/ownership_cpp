// expect: no matching (function for call to|constructor for initialization of) '(own::)?unique_owner<int, ?(\[\.\.\.\]|(own::)?allocator_ref)>
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto source = std::make_unique<int>(1); own::unique_owner<int, own::allocator_ref> owner(std::move(source)); }

// expect: (no match for 'operator=' \(operand types are '(own::)?unique_owner<int(, ?(\(default\) )?(\[\.\.\.\]|(own::)?new_delete))?>'|no viable overloaded '=')
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto source = own::make_unique<const int>(1); own::unique_owner<int> owner; owner = std::move(source); }

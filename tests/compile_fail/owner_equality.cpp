// expect: (no match for 'operator==' \(operand types are '(own::)?shared_owner<int>|invalid operands to binary expression \('(own::)?shared_owner<int>)
#include <own/ownership.hpp>
// Owners compare only with nullptr, never with each other.
int main() { auto a = own::make_shared<int>(1); auto b = a; (void)(a == b); }

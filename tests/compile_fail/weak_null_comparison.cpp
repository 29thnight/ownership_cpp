// expect: (no match for 'operator==' \(operand types are '(own::)?weak_owner<int>|invalid operands to binary expression \('(own::)?weak_owner<int>)
#include <own/ownership.hpp>
int main() { own::weak_owner<int> weak; (void)(weak == nullptr); }

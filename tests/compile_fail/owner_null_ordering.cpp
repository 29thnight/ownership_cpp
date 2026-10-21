// expect: (no match for 'operator<' \(operand types are '(own::)?shared_owner<int>|invalid operands to binary expression \('(own::)?shared_owner<int>)
#include <own/ownership.hpp>
int main() { auto a = own::make_shared<int>(1); (void)(a < nullptr); }

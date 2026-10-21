// expect: (no match for 'operator==' \(operand types are '(own::)?(local_view|borrowed)<int>|invalid operands to binary expression \('(own::)?(local_view|borrowed)<int>)
#include <own/ownership.hpp>
int main() { int value = 1; (void)value; own::local_view<int> a, b; (void)(a == b); }

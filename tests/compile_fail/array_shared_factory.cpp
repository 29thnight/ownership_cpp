// expect: static assert
// expect: is_array
#include <own/ownership.hpp>
int main() { auto invalid = own::make_shared<int[]>(3); }

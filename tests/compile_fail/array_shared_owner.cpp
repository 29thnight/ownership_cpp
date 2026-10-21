// expect: static assert
// expect: is_array
#include <own/ownership.hpp>
int main() { own::shared_owner<int[]> invalid; }

// expect: static assert
// expect: is_array
#include <own/ownership.hpp>
int main() { own::weak_owner<int[]> invalid; }

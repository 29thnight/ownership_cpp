// expect: static assert
// expect: is_array
#include <own/ownership.hpp>
int main() { own::local_view<int[]> invalid; }

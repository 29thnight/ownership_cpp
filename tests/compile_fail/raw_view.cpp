// expect: (is private within this context|calling a private constructor)
// expect: borrowed
#include <own/ownership.hpp>
int main() { int value = 1; own::local_view<int> invalid(&value); }

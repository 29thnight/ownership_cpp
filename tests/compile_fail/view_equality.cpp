#include <own/ownership.hpp>
int main() { int value = 1; (void)value; own::local_view<int> a, b; (void)(a == b); }

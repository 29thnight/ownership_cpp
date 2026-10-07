#include <own/ownership.hpp>
int main() { int value = 1; own::local_view<int> invalid(&value); }

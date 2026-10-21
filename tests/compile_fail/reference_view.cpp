// expect: no matching (function for call to|constructor for initialization of) '(own::)?(borrowed|local_view)<int>
#include <own/ownership.hpp>
int main() { int value = 1; own::local_view<int> invalid(value); }

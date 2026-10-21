// expect: no matching (function for call to|constructor for initialization of) '(own::)?local_owner<int>
#include <own/ownership.hpp>
int main() { own::local_owner<int> invalid(new int(1)); }

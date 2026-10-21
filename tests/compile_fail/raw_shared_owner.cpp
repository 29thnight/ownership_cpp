// expect: no matching (function for call to|constructor for initialization of) '(own::)?shared_owner<int>
#include <own/ownership.hpp>
int main() { own::shared_owner<int> invalid(new int(1)); }

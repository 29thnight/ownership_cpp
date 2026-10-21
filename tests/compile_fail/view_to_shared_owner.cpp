// expect: no matching (function for call to|constructor for initialization of) '(own::)?shared_owner<int>
#include <own/ownership.hpp>
int main() { auto owner = own::make_shared<int>(1); own::shared_owner<int> invalid(owner.borrow()); }

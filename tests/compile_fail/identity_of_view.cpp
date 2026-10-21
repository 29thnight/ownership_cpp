// expect: no matching function for call to '?identity
#include <own/ownership.hpp>

int main() { auto owner = own::make_shared<int>(1); auto id = own::identity(owner.borrow()); }

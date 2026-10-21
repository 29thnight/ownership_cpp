// expect: no matching function for call to '?static_owner_cast
#include <own/ownership.hpp>

int main() { auto source = own::make_shared<const int>(1); auto target = own::static_owner_cast<int>(source); }

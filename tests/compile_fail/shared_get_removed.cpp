// expect: (no member named 'get'|'get' is not a member)
#include <own/ownership.hpp>
int main() { auto owner = own::make_shared<int>(1); (void)owner.get(); }

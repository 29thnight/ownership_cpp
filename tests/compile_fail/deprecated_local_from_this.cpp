// expect: local_from_this.*deprecated
// expect: use borrow_from_this\(\)
#include <own/ownership.hpp>

struct self : own::enable_owner_from_this<self> {};
int main() { self object; return object.local_from_this() ? 1 : 0; }

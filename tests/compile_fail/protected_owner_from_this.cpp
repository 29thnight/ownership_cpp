// expect: static assert
// expect: enable_owner_from_this\ must\ be\ a\ single\ public\ unambiguous\ base
#include <own/ownership.hpp>
class self : protected own::enable_owner_from_this<self> {};
int main() { auto owner = own::make_shared<self>(); }

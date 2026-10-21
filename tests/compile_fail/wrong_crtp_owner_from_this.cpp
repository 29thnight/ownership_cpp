// expect: static assert
// expect: enable_owner_from_this\ must\ name\ a\ public\ unambiguous\ object\ base
#include <own/ownership.hpp>
struct unrelated {};
struct self : own::enable_owner_from_this<unrelated> {};
int main() { auto owner = own::make_shared<self>(); }

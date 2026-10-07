#include <own/ownership.hpp>
struct other : own::enable_owner_from_this<other> {};
struct self : own::enable_owner_from_this<self>, private other {};
int main() { auto owner = own::make_shared<self>(); }

#include <own/ownership.hpp>
struct self;
struct left : own::enable_owner_from_this<self> {};
struct right : own::enable_owner_from_this<self> {};
struct self : left, right {};
int main() { auto owner = own::make_shared<self>(); }

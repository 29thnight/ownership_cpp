#include <own/ownership.hpp>
class self : private own::enable_owner_from_this<self> {};
int main() { auto owner = own::make_shared<self>(); }

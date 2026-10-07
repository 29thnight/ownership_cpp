#include <own/ownership.hpp>
#include <utility>
struct self : own::enable_owner_from_this<self> {};
int main() { self object; (void)std::move(object).borrow_from_this(); }

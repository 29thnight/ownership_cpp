#include <own/ownership.hpp>
#include <utility>
struct self : own::enable_owner_from_this<self> {};
int main() { const self object; (void)std::move(object).local_from_this(); }

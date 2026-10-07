#include <own/ownership.hpp>
int main() { auto owner = own::make_shared<int>(1); own::local_owner<int> invalid(owner.borrow()); }

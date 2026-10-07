#include <own/ownership.hpp>
int main() { auto owner = own::make_shared<const int>(1); own::local_view<int> invalid = owner.borrow(); }

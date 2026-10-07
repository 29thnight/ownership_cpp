#include <own/ownership.hpp>
#include <utility>
int main() { auto owner = own::make_local<int>(1); (void)std::move(owner).borrow(); }

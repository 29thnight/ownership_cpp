#include <own/ownership.hpp>
#include <utility>
int main() { auto owner = own::make_shared<int>(1); (void)std::move(owner).borrow(); }

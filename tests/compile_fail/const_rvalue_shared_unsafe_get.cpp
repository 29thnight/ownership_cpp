#include <own/ownership.hpp>
#include <utility>
int main() { const auto owner = own::make_shared<int>(1); (void)std::move(owner).unsafe_get(); }

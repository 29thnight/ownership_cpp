#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { const auto owner = own::allocate_unique<int>({}, 1); auto view = std::move(owner).borrow(); }

#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { own::weak_owner<int> source; own::unique_owner<int> owner(std::move(source)); }

#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { int value = 1; own::unique_owner<int> owner(value); }

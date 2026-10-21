// expect: (is private within this context|calling a private constructor)
// expect: unique_owner
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { int* value = nullptr; own::unique_owner<int> owner(value); }

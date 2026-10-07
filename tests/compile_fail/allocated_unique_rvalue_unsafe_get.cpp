#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto raw = own::allocate_unique<int>({}, 1).unsafe_get(); }

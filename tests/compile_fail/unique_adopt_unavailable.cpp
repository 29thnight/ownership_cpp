#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto owner = own::unique_owner<int>::adopt(new int(1)); }

// expect: (no member named 'adopt'|'adopt' is not a member)
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto owner = own::allocated_unique_owner<int>::adopt(new int(1)); }

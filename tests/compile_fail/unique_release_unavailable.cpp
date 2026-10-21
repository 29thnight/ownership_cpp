// expect: (no member named 'release'|'release' is not a member)
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto owner = own::make_unique<int>(1); auto raw = owner.release(); }

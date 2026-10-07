#include <own/ownership.hpp>
int main() { int value = 1; own::shared_owner<int> owner; owner.reset(&value); }

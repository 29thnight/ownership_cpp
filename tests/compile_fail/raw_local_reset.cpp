#include <own/ownership.hpp>
int main() { int value = 1; own::local_owner<int> owner; owner.reset(&value); }

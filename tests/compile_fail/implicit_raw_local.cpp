#include <own/ownership.hpp>
int main() { auto owner = own::make_local<int>(1); int* invalid = owner; (void)invalid; }

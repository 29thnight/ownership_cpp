#include <own/ownership.hpp>
int main() { auto owner = own::make_shared<int>(1); int* invalid = owner.borrow(); (void)invalid; }

#include <own/ownership.hpp>
// Owners compare only with nullptr, never with each other.
int main() { auto a = own::make_shared<int>(1); auto b = a; (void)(a == b); }

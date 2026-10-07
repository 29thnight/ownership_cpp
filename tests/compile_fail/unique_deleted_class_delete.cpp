#include <own/ownership.hpp>
#include <utility>

struct value { static void operator delete(void*) = delete; };
int main() { auto owner = own::make_unique<value>(); }

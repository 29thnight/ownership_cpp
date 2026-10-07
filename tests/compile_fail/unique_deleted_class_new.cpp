#include <own/ownership.hpp>
#include <utility>

struct value { static void* operator new(std::size_t) = delete; };
int main() { auto owner = own::make_unique<value>(); }

#include <own/ownership.hpp>
#include <utility>

struct base {}; struct derived : base {};
int main() { auto source = own::make_unique<derived>(); own::unique_owner<base> target; target = std::move(source); }

#include <own/ownership.hpp>
#include <memory>
#include <utility>

struct base { virtual ~base() noexcept = default; }; struct derived : base {};
int main() { own::allocated_unique_owner<base> source; own::allocated_unique_owner<derived> target = std::move(source); }

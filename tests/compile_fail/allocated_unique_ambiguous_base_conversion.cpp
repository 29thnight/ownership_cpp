#include <own/ownership.hpp>
#include <memory>
#include <utility>

struct base { virtual ~base() noexcept = default; }; struct left : base {}; struct right : base {}; struct derived : left, right {};
int main() { own::allocated_unique_owner<derived> source; own::allocated_unique_owner<base> target = std::move(source); }

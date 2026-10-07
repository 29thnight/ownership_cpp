#include <own/ownership.hpp>
#include <memory>
#include <utility>

struct base { virtual ~base() noexcept = default; }; struct derived : base {};
int main() { own::unique_owner<base> source; own::unique_owner<derived> target = std::move(source); }

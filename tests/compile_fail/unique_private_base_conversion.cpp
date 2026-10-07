#include <own/ownership.hpp>
#include <memory>
#include <utility>

struct base { virtual ~base() noexcept = default; }; class derived : base {};
int main() { own::unique_owner<derived> source; own::unique_owner<base> target = std::move(source); }

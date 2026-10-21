// expect: no matching function for call to '?dynamic_owner_cast
#include <own/ownership.hpp>

struct base { virtual ~base() noexcept = default; }; struct derived : base {};
int main() { own::unique_owner<base> source = own::make_unique<derived>(); auto target = own::dynamic_owner_cast<derived>(source); }

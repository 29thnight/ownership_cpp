// expect: no matching function for call to '?dynamic_owner_cast
#include <own/ownership.hpp>
#include <utility>

// Polymorphic, but without a virtual destructor: deleting through derived* is unsafe.
struct base { virtual void touch() {} }; struct derived : base {};
int main() { own::unique_owner<base> source; auto target = own::dynamic_owner_cast<derived>(std::move(source)); }

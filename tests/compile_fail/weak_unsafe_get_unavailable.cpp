// expect: (no member named 'unsafe_get'|'unsafe_get' is not a member)
#include <own/ownership.hpp>
int main() { own::weak_owner<int> weak; (void)weak.unsafe_get(); }

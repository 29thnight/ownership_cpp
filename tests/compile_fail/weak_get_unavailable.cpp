// expect: (no member named 'get'|'get' is not a member)
#include <own/ownership.hpp>
int main() { own::weak_owner<int> weak; (void)weak.get(); }

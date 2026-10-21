// expect: (no member named 'borrow'|'borrow' is not a member)
#include <own/ownership.hpp>
int main() { own::weak_owner<int> weak; (void)weak.borrow(); }

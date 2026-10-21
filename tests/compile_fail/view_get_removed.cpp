// expect: (no member named 'get'|'get' is not a member)
#include <own/ownership.hpp>
int main() { own::local_view<int> view; (void)view.get(); }

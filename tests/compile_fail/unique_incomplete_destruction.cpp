// expect: invalid application of 'sizeof' to (an )?incomplete type
#include <own/ownership.hpp>
#include <utility>

struct incomplete;
int main() { own::unique_owner<incomplete> owner; }

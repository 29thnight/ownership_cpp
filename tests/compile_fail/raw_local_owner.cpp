#include <own/ownership.hpp>
int main() { own::local_owner<int> invalid(new int(1)); }

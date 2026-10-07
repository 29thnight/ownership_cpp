#include <own/ownership.hpp>
int main() { own::shared_owner<int> invalid = own::make_local<int>(1); }

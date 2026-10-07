#include <own/ownership.hpp>
int main() { own::shared_owner<int> invalid = own::make_shared<const int>(1); }

#include <own/ownership.hpp>
int main() { own::shared_owner<int> invalid(new int(1)); }

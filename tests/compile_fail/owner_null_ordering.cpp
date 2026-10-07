#include <own/ownership.hpp>
int main() { auto a = own::make_shared<int>(1); (void)(a < nullptr); }

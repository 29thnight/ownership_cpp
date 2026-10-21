#include <own/ownership.hpp>
int from_a();
int main() { return from_a() + *own::make_shared<int>(1) == 2 ? 0 : 1; }

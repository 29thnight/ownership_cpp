#include <own/ownership.hpp>
int from_a() { return *own::make_shared<int>(1); }

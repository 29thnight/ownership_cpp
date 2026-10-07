#include <own/ownership.hpp>
int main() { auto invalid = own::make_local<int[]>(3); }

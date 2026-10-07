#include <own/ownership.hpp>
int main() { auto invalid = own::make_shared<int[3]>(); }

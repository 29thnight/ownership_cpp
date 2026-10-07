#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto owner = own::make_unique<int[3]>(); }

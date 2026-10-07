#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto view = own::make_unique<int>(1).borrow(); }

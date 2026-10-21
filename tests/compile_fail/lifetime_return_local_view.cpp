// only: clang
// expect: address of stack memory associated with local variable 'owner' returned
#include <own/ownership.hpp>

own::borrowed<int> dangling() {
    auto owner = own::make_shared<int>(1);
    return owner.borrow(); // the owner dies at return
}
int main() { return dangling() ? 0 : 1; }

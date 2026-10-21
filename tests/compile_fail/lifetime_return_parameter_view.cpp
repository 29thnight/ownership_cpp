// only: clang
// expect: address of stack memory associated with parameter 'owner' returned
#include <own/ownership.hpp>

own::borrowed<int> dangling(own::unique_owner<int> owner) {
    return owner.borrow(); // the by-value parameter dies at return
}
int main() { return dangling(own::make_unique<int>(1)) ? 0 : 1; }

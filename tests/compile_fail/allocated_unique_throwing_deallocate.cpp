#include <own/ownership.hpp>
#include <utility>

void deallocate(void*, void*, std::size_t, std::size_t) {}
int main() { own::allocator_ref allocator; allocator.deallocate = &deallocate; auto owner = own::allocate_unique<int>(allocator, 1); }

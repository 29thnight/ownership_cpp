// expect: static assert
// expect: allocator deriving from own::allocator_interface<Alloc>
#include <own/ownership.hpp>
#include <cstddef>
#include <new>

// The right member functions are not enough: the interface is the contract.
struct pool {
    void* do_allocate(std::size_t size, std::size_t) { return ::operator new(size); }
    void do_deallocate(void* p, std::size_t, std::size_t) noexcept { ::operator delete(p); }
};
int main() { own::unique_owner<int, pool> owner; }

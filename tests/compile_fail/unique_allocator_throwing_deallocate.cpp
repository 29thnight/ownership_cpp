// expect: static assert
// expect: unique_owner's second argument must be own::new_delete, own::allocator_ref, or a type
#include <own/ownership.hpp>
#include <cstddef>
#include <new>

struct pool {
    void* allocate(std::size_t size, std::size_t) { return ::operator new(size); }
    void deallocate(void* p, std::size_t, std::size_t) { ::operator delete(p); } // not noexcept
};
int main() { own::unique_owner<int, pool> owner; }

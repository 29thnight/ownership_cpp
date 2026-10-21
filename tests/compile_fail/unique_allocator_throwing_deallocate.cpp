// expect: static assert
// expect: unique_owner's second argument must be own::new_delete, own::allocator_ref, or an
#include <own/ownership.hpp>
#include <cstddef>
#include <new>

struct pool : own::allocator_interface<pool> {
    void* do_allocate(std::size_t size, std::size_t) { return ::operator new(size); }
    void do_deallocate(void* p, std::size_t, std::size_t) { ::operator delete(p); } // not noexcept
};
int main() { own::unique_owner<int, pool> owner; }

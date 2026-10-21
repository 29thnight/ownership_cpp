// expect: no matching (function for call to|constructor for initialization of) '(own::)?unique_owner<int, pool_handle>
#include <own/ownership.hpp>
#include <cstddef>
#include <new>

struct pool_handle {
    void* state = nullptr;
    void* allocate(std::size_t size, std::size_t) { return ::operator new(size); }
    void deallocate(void* p, std::size_t, std::size_t) noexcept { ::operator delete(p); }
};
// A stateful allocator has no meaningful default instance: pass it instead.
int main() { own::unique_owner<int, pool_handle> owner(std::in_place, 1); }

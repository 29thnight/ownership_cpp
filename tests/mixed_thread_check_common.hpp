#pragma once
#include "mixed_thread_check.hpp"
#include <own/ownership.hpp>
#include <new>

namespace mixed {
// Records the size/alignment of every allocation and rejects mismatched frees.
struct record { void* pointer; std::size_t size, alignment; };
inline record records[16];

inline void* allocate(void* context, std::size_t size, std::size_t alignment) {
    auto& state = *static_cast<mixed_allocations*>(context);
    for (auto& entry : records) {
        if (!entry.pointer) {
            entry = {::operator new(size, std::align_val_t(alignment)), size, alignment};
            ++state.live;
            state.bytes += size;
            return entry.pointer;
        }
    }
    return nullptr;
}

inline void deallocate(void* context, void* pointer, std::size_t size, std::size_t alignment) noexcept {
    auto& state = *static_cast<mixed_allocations*>(context);
    for (auto& entry : records) {
        if (entry.pointer == pointer) {
            if (entry.size != size || entry.alignment != alignment) { ++state.mismatches; }
            ::operator delete(pointer, std::align_val_t(entry.alignment));
            entry = {};
            --state.live;
            state.bytes -= size;
            return;
        }
    }
    ++state.mismatches;
}

inline own::allocator_ref tracking(mixed_allocations& state) { return {&state, allocate, deallocate}; }
} // namespace mixed

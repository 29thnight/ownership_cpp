// Regression: debug thread checks change behavior, never the local group layout.
// Before the fix, a group created without checks was 8 bytes smaller than the
// checked layout, so the checked side read past the allocation (ASan
// heap-buffer-overflow) and deallocated with a mismatched size.
#include "mixed_thread_check.hpp"
#include <cstdio>

int main() {
    auto off = layout_without_checks();
    auto on = layout_with_checks();
    if (off.size != on.size || off.alignment != on.alignment) {
        std::fprintf(stderr, "FAIL local_group layout differs: %zu/%zu vs %zu/%zu\n",
                     off.size, off.alignment, on.size, on.alignment);
        return 1;
    }
    mixed_allocations allocations;
    if (!exchange_from_unchecked(allocations) || !exchange_from_checked(allocations)) {
        std::fprintf(stderr, "FAIL mixed-configuration local owner exchange\n");
        return 1;
    }
    if (allocations.live != 0 || allocations.bytes != 0 || allocations.mismatches != 0) {
        std::fprintf(stderr, "FAIL mixed-configuration allocations: live=%zu bytes=%zu mismatches=%zu\n",
                     allocations.live, allocations.bytes, allocations.mismatches);
        return 1;
    }
    std::puts("PASS mixed thread-check configurations share one local group layout");
    return 0;
}

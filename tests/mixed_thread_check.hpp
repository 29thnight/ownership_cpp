#pragma once
// Translation units in one program may disagree on NDEBUG and therefore on
// OWN_DEBUG_THREAD_CHECK. Each side reports its own view of the layout and
// exchanges local owners with the other side through these functions.
#include <cstddef>

struct mixed_layout { std::size_t size, alignment; };
struct mixed_allocations { std::size_t live = 0, bytes = 0, mismatches = 0; };

mixed_layout layout_without_checks();
mixed_layout layout_with_checks();
// Each creates a local owner in its own configuration, hands copies to the other
// configuration and destroys the last alias there. Allocations use a tracking
// allocator that requires exact size/alignment matching on deallocation.
bool exchange_from_unchecked(mixed_allocations& allocations);
bool exchange_from_checked(mixed_allocations& allocations);

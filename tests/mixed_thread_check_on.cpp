#define OWN_DEBUG_THREAD_CHECK 1
#include "mixed_thread_check_common.hpp"

own::local_owner<int> make_unchecked(mixed_allocations& allocations);
bool consume_unchecked(own::local_owner<int> owner);
own::local_owner<int> make_checked(mixed_allocations& allocations) {
    return own::allocate_local<int>(mixed::tracking(allocations), 31);
}
bool consume_checked(own::local_owner<int> owner) {
    auto copy = owner;
    return *copy == 47 && copy.local_use_count() == 2;
}

mixed_layout layout_with_checks() {
    return {sizeof(own::detail::local_group), alignof(own::detail::local_group)};
}
bool exchange_from_checked(mixed_allocations& allocations) {
    auto owner = make_checked(allocations);
    *owner = 47;
    return consume_unchecked(std::move(owner)) && !owner;
}

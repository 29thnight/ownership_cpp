#define OWN_DEBUG_THREAD_CHECK 0
#include "mixed_thread_check_common.hpp"

own::local_owner<int> make_unchecked(mixed_allocations& allocations) {
    return own::allocate_local<int>(mixed::tracking(allocations), 31);
}
bool consume_unchecked(own::local_owner<int> owner) {
    auto copy = owner;
    return *copy == 47 && copy.local_use_count() == 2;
}
own::local_owner<int> make_checked(mixed_allocations& allocations);
bool consume_checked(own::local_owner<int> owner);

mixed_layout layout_without_checks() {
    return {sizeof(own::detail::local_group), alignof(own::detail::local_group)};
}
bool exchange_from_unchecked(mixed_allocations& allocations) {
    auto owner = make_unchecked(allocations);
    *owner = 47;
    return consume_checked(std::move(owner)) && !owner;
}

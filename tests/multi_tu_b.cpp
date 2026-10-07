#include "multi_tu.hpp"

own::unique_owner<const int> transfer_unique_in_second_translation_unit(own::unique_owner<int> value) {
    return value;
}

own::allocated_unique_owner<const int> transfer_allocated_unique_in_second_translation_unit(own::allocated_unique_owner<int> value) {
    return value;
}

own::weak_owner<int> observe_in_second_translation_unit(const own::shared_owner<int>& value) {
    auto local = value.localize();
    return own::weak_owner<int>(local);
}

int read_borrow_in_second_translation_unit(own::local_view<const int> value) { return *value; }

own::shared_owner<multi_tu_self> retain_self_in_second_translation_unit(own::local_view<multi_tu_self> value) {
    return value->shared_from_this();
}

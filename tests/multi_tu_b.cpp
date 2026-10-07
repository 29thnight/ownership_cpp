#include "multi_tu.hpp"

own::weak_owner<int> observe_in_second_translation_unit(const own::shared_owner<int>& value) {
    auto local = value.localize();
    return own::weak_owner<int>(local);
}

#include "multi_tu.hpp"

own::unique_owner<int> make_unique_in_first_translation_unit() {
    return own::make_unique<int>(83);
}

own::allocated_unique_owner<int> make_allocated_unique_in_first_translation_unit() {
    return own::allocate_unique<int>({}, 84);
}

own::shared_owner<int> make_in_first_translation_unit() {
    return own::make_shared<int>(76);
}

own::shared_owner<multi_tu_self> make_self_in_first_translation_unit() {
    return own::make_shared<multi_tu_self>();
}

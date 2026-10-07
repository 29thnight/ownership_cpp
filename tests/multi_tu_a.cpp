#include "multi_tu.hpp"

own::shared_owner<int> make_in_first_translation_unit() {
    return own::make_shared<int>(76);
}

own::shared_owner<multi_tu_self> make_self_in_first_translation_unit() {
    return own::make_shared<multi_tu_self>();
}

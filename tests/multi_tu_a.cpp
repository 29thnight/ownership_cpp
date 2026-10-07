#include "multi_tu.hpp"

own::shared_owner<int> make_in_first_translation_unit() {
    return own::make_shared<int>(76);
}

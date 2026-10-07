#include "multi_tu.hpp"

int main() {
    auto value = make_in_first_translation_unit();
    auto weak = observe_in_second_translation_unit(value);
    if (*weak.lock() != 76) return 1;
    value.reset();
    return weak.expired() ? 0 : 1;
}

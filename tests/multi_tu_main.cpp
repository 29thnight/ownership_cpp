#include "multi_tu.hpp"

int main() {
    auto value = make_in_first_translation_unit();
    auto weak = observe_in_second_translation_unit(value);
    if (*weak.lock() != 76 || read_borrow_in_second_translation_unit(value.borrow()) != 76) return 1;
    auto self = make_self_in_first_translation_unit();
    auto retained = retain_self_in_second_translation_unit(self.borrow());
    if (self.use_count() != 2 || retained->value != 91) return 1;
    self.reset();
    if (retained.use_count() != 1 || retained->borrow_from_this()->value != 91) return 1;
    value.reset();
    return weak.expired() ? 0 : 1;
}

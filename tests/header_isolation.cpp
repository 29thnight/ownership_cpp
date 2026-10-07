#include <own/ownership.hpp>

struct incomplete;
struct incomplete_holder {
    own::local_owner<incomplete> local;
    own::shared_owner<incomplete> shared;
    own::weak_owner<incomplete> weak;
    own::local_view<incomplete> view;
};

// Form, operate on, and destroy empty handles before the pointee is complete.
int uses_incomplete_type() {
    incomplete_holder value;
    return value.local || value.shared || value.view || !value.weak.expired() ? 1 : 0;
}

struct incomplete { int value; };

int main() {
    auto value = own::make_local<int>(42);
    auto shared = value.share();
    own::weak_owner<int> weak = shared;
    auto view = shared.borrow();
    return *weak.lock() == 42 && *view == 42 && uses_incomplete_type() == 0 ? 0 : 1;
}

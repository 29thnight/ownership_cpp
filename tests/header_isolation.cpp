#include <own/ownership.hpp>

struct permanently_incomplete;
int uses_permanently_incomplete_allocated() {
    own::allocated_unique_owner<permanently_incomplete> value;
    auto moved = static_cast<own::allocated_unique_owner<permanently_incomplete>&&>(value);
    moved.reset();
    return value || moved ? 1 : 0;
}

struct incomplete;
// A declaration/forwarded opaque interface needs no complete payload.
own::unique_owner<incomplete> declared_opaque_factory();
void declared_opaque_consumer(own::unique_owner<incomplete>);
static_assert(sizeof(own::unique_owner<incomplete>) == sizeof(incomplete*));
struct incomplete_holder {
    own::allocated_unique_owner<incomplete> unique;
    own::local_owner<incomplete> local;
    own::shared_owner<incomplete> shared;
    own::weak_owner<incomplete> weak;
    own::local_view<incomplete> view;
};

// Form, operate on, and destroy empty handles before the pointee is complete.
int uses_incomplete_type() {
    incomplete_holder value;
    auto moved = static_cast<own::allocated_unique_owner<incomplete>&&>(value.unique);
    moved.reset();
    return value.unique || moved || value.local || value.shared || value.view || !value.weak.expired() ? 1 : 0;
}

struct incomplete { int value; };

int main() {
    auto unique = own::make_unique<int>(42);
    auto allocated = own::allocate_unique<int>({}, 42);
    auto value = own::make_local<int>(42);
    auto shared = value.share();
    own::weak_owner<int> weak = shared;
    auto view = shared.borrow();
    return *unique.borrow() == 42 && *allocated.borrow() == 42 && *weak.lock() == 42 && *view == 42 && uses_incomplete_type() == 0 && uses_permanently_incomplete_allocated() == 0 ? 0 : 1;
}

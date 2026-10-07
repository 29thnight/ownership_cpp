#include <own/ownership.hpp>
#include <cassert>
#include <cstdio>

namespace
{
    struct node : own::enable_owner_from_this<node>
    {
        int value = 42;
        node() { assert(!shared_from_this()); } // not registered until construction ends
        ~node() { assert(!shared_from_this()); } // never resurrect during destruction
        own::shared_owner<node> retain() { return shared_from_this(); }
        own::local_view<const node> inspect() const { return local_from_this(); }
    };
}

int main()
{
    auto owner = own::make_shared<node>();
    auto job_owner = owner->retain();
    auto view = job_owner->inspect();
    own::weak_owner<node> observed = owner->weak_from_this();
    owner.reset();
    assert(view->value == 42); // job_owner still owns the complete use
    view.reset();
    job_owner.reset();
    assert(observed.expired());
    node unmanaged;
    assert(!unmanaged.shared_from_this());
    assert(!unmanaged.local_from_this());
    std::puts("same-control ownership from this passed");
}

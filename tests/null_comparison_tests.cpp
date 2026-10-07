#include <own/ownership.hpp>

#include "test_support.hpp"

#include <type_traits>
#include <utility>

namespace {
template<class Handle>
void check_null_comparisons(Handle& filled) {
    Handle empty;
    CHECK(empty == nullptr && nullptr == empty && !(empty != nullptr) && !(nullptr != empty));
    CHECK(filled != nullptr && nullptr != filled && !(filled == nullptr) && !(nullptr == filled));
    CHECK((filled == nullptr) == !filled);
}

template<class A, class B>
concept equality_comparable_with_each_other = requires(const A& a, const B& b) { a == b; };
template<class A>
concept orderable = requires(const A& a) { a < a; };
template<class A>
concept null_orderable = requires(const A& a) { a < nullptr; };
} // namespace

// Only null comparison is added: handles are not compared with each other or
// ordered, and weak observers have no null state to compare.
static_assert(!equality_comparable_with_each_other<own::shared_owner<int>, own::shared_owner<int>>);
static_assert(!equality_comparable_with_each_other<own::local_view<int>, own::local_view<int>>);
static_assert(!equality_comparable_with_each_other<own::unique_owner<int>, own::unique_owner<int>>);
static_assert(!equality_comparable_with_each_other<own::weak_owner<int>, std::nullptr_t>);
static_assert(!orderable<own::shared_owner<int>> && !null_orderable<own::shared_owner<int>>);
static_assert(!orderable<own::local_view<int>> && !null_orderable<own::local_view<int>>);
static_assert(own::local_view<int>() == nullptr, "view null comparison is constexpr");

int main() {
    test::run("shared owner null comparison", [] {
        auto owner = own::make_shared<int>(1);
        check_null_comparisons(owner);
        owner.reset();
        CHECK(owner == nullptr);
    });
    test::run("local owner null comparison", [] {
        auto owner = own::make_local<int>(2);
        check_null_comparisons(owner);
    });
    test::run("unique owner null comparison", [] {
        auto owner = own::make_unique<int>(3);
        check_null_comparisons(owner);
        auto moved = std::move(owner);
        CHECK(owner == nullptr && moved != nullptr);
    });
    test::run("allocated unique owner null comparison", [] {
        auto owner = own::allocate_unique<int>({}, 4);
        check_null_comparisons(owner);
    });
    test::run("view null comparison", [] {
        auto owner = own::make_shared<int>(5);
        auto view = owner.borrow();
        check_null_comparisons(view);
        own::local_view<const int> constant = view;
        CHECK(constant != nullptr);
        view.reset();
        CHECK(view == nullptr);
    });
    return test::finish();
}

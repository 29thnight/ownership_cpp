#include <own/ownership.hpp>

#include <type_traits>
#include <utility>

// Compile with OWN_ENABLE_UNSAFE_GET_WARNING=0 so testing the existence of
// the deliberately deprecated escape hatch does not suppress warnings elsewhere.
template<class T> concept has_unsafe_get = requires(T& object) { object.unsafe_get(); };
template<class T> concept has_rvalue_unsafe_get = requires(T&& object) { std::move(object).unsafe_get(); };
static_assert(has_unsafe_get<own::shared_owner<int>>);
static_assert(has_unsafe_get<const own::shared_owner<int>>);
static_assert(has_unsafe_get<own::local_owner<int>>);
static_assert(has_unsafe_get<const own::local_owner<int>>);
static_assert(has_unsafe_get<own::local_view<int>>);
static_assert(has_unsafe_get<const own::local_view<int>>);
static_assert(!has_unsafe_get<own::weak_owner<int>>);
static_assert(!has_rvalue_unsafe_get<own::shared_owner<int>>);
static_assert(!has_rvalue_unsafe_get<const own::shared_owner<int>>);
static_assert(!has_rvalue_unsafe_get<own::local_owner<int>>);
static_assert(!has_rvalue_unsafe_get<const own::local_owner<int>>);
static_assert(has_rvalue_unsafe_get<own::local_view<int>>);
static_assert(has_rvalue_unsafe_get<const own::local_view<int>>);
static_assert(std::is_same_v<decltype(std::declval<const own::shared_owner<const int>&>().unsafe_get()), const int*>);
static_assert(std::is_same_v<decltype(std::declval<const own::local_owner<const int>&>().unsafe_get()), const int*>);
static_assert(std::is_same_v<decltype(std::declval<const own::local_view<const int>&>().unsafe_get()), const int*>);
static_assert(noexcept(std::declval<const own::shared_owner<int>&>().unsafe_get()));
static_assert(noexcept(std::declval<const own::local_owner<int>&>().unsafe_get()));
static_assert(noexcept(std::declval<const own::local_view<int>&>().unsafe_get()));
int main() {
    own::shared_owner<int> shared;
    own::local_owner<int> local;
    own::local_view<int> view;
    return shared.unsafe_get() == nullptr && local.unsafe_get() == nullptr &&
           view.unsafe_get() == nullptr ? 0 : 1;
}

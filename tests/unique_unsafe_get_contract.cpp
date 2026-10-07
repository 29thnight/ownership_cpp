#include <own/ownership.hpp>

#include <type_traits>
#include <utility>

// This dedicated signature test is built with OWN_ENABLE_UNSAFE_GET_WARNING=0.
template<class T> concept has_unsafe_get = requires(T& owner) { owner.unsafe_get(); };
template<class T> concept has_rvalue_unsafe_get = requires(T&& owner) { std::move(owner).unsafe_get(); };
static_assert(has_unsafe_get<own::unique_owner<int>>);
static_assert(has_unsafe_get<const own::unique_owner<int>>);
static_assert(!has_rvalue_unsafe_get<own::unique_owner<int>>);
static_assert(!has_rvalue_unsafe_get<const own::unique_owner<int>>);
static_assert(std::is_same_v<decltype(std::declval<const own::unique_owner<const int>&>().unsafe_get()), const int*>);
static_assert(std::is_same_v<decltype(std::declval<const own::unique_owner<int>&>().unsafe_get()), int*>);
static_assert(noexcept(std::declval<const own::unique_owner<int>&>().unsafe_get()));

int main() {
    own::unique_owner<int> empty;
    auto owner = own::make_unique<int>(42);
    int* pointer = owner.unsafe_get();
    own::unique_owner<const int> moved = std::move(owner);
    return empty.unsafe_get() == nullptr && owner.unsafe_get() == nullptr &&
           moved.unsafe_get() == pointer && *pointer == 42 ? 0 : 1;
}

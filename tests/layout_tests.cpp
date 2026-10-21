// Size and layout claims made in the README, checked by the compiler on every
// platform that builds the tests. A failure here means the documentation (or
// the platform's layout) changed, not that ownership is unsafe.
#include <own/ownership.hpp>

#include <cstdint>
#include <cstdio>

namespace {
constexpr std::size_t word = sizeof(void*);
struct payload { std::uint64_t value; };

// Handles.
static_assert(sizeof(own::borrowed<payload>) == word, "a view is one pointer");
static_assert(sizeof(own::unique_owner<payload>) == word, "unique_owner is one pointer");
static_assert(sizeof(own::shared_owner<payload>) == 2 * word);
static_assert(sizeof(own::weak_owner<payload>) == 2 * word);
static_assert(sizeof(own::local_owner<payload>) == 2 * word);
static_assert(sizeof(own::unique_owner<payload, own::allocator_ref>) == 5 * word);
static_assert(sizeof(own::object_id) == sizeof(std::uintptr_t));

// Control blocks: one 64-bit count word and one ops pointer.
static_assert(sizeof(own::detail::control_block) == 16, "compact 16-byte header");
// make_shared: header immediately followed by the payload. This is what needs
// OWN_NO_UNIQUE_ADDRESS to take effect (MSVC ignores the standard spelling).
static_assert(sizeof(own::detail::in_place_control<payload>) ==
                  sizeof(own::detail::control_block) + sizeof(payload),
              "make_shared block is header + payload");
// make_local: header, the embedded first group, payload.
static_assert(sizeof(own::detail::in_place_control<payload, true>) ==
              sizeof(own::detail::control_block) + sizeof(own::detail::local_group) + sizeof(payload));
static_assert(sizeof(own::detail::local_group) == 5 * word, "local group is five words");
} // namespace

int main() {
    std::printf("PASS layout: view %zu, unique %zu, shared %zu, allocated_unique %zu, "
                "make_shared block %zu, make_local block %zu bytes\n",
                sizeof(own::borrowed<payload>), sizeof(own::unique_owner<payload>),
                sizeof(own::shared_owner<payload>), sizeof(own::unique_owner<payload, own::allocator_ref>),
                sizeof(own::detail::in_place_control<payload>),
                sizeof(own::detail::in_place_control<payload, true>));
    return 0;
}

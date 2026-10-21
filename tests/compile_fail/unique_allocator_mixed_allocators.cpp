// expect: conversion from '[^']*unique_owner<(int|\[\.\.\.\]), ?(own::)?allocator_ref>
// expect: to (non-scalar type )?'[^']*unique_owner<(int|\[\.\.\.\])(, ?(\(default\) )?(own::)?new_delete)?>'
#include <own/ownership.hpp>
#include <utility>

// Owners with different allocators are different types.
int main() { auto source = own::allocate_unique<int>({}, 1); own::unique_owner<int> target = std::move(source); }

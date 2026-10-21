// expect: conversion from '[^']*unique_owner<const int, ?(\[\.\.\.\]|(own::)?allocator_ref)>
// expect: to (non-scalar type )?'(own::)?unique_owner<int, ?(\[\.\.\.\]|(own::)?allocator_ref)>'
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto source = own::allocate_unique<const int>({}, 1); own::unique_owner<int, own::allocator_ref> owner = std::move(source); }

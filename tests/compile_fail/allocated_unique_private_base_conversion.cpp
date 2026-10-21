// expect: conversion from '[^']*unique_owner<derived, ?(\[\.\.\.\]|(own::)?allocator_ref)>
// expect: to (non-scalar type )?'(own::)?unique_owner<base, ?(\[\.\.\.\]|(own::)?allocator_ref)>'
#include <own/ownership.hpp>
#include <memory>
#include <utility>

struct base { virtual ~base() noexcept = default; }; class derived : base {};
int main() { own::unique_owner<derived, own::allocator_ref> source; own::unique_owner<base, own::allocator_ref> target = std::move(source); }

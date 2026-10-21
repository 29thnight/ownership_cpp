// expect: conversion from '[^']*unique_owner<derived, ?(\[\.\.\.\]|pool)
// expect: to (non-scalar type )?'[^']*unique_owner<base, ?(\[\.\.\.\]|pool)>'
#include <own/ownership.hpp>
#include <cstddef>
#include <new>
#include <utility>

struct pool : own::allocator_interface<pool> {
    void* do_allocate(std::size_t size, std::size_t) { return ::operator new(size); }
    void do_deallocate(void* p, std::size_t, std::size_t) noexcept { ::operator delete(p); }
};
struct base { virtual ~base() noexcept = default; }; struct derived : base { int x = 0; };
// The pool would be handed sizeof(base) for an object of sizeof(derived).
int main() { own::unique_owner<derived, pool> source(std::in_place); own::unique_owner<base, pool> target = std::move(source); }

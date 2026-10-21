// expect: conversion from '[^']*unique_owner<base(, ?(\(default\) )?(\[\.\.\.\]|(own::)?new_delete))?>
// expect: to (non-scalar type )?'(own::)?unique_owner<derived(, ?(\(default\) )?(\[\.\.\.\]|(own::)?new_delete))?>'
#include <own/ownership.hpp>
#include <memory>
#include <utility>

struct base { virtual ~base() noexcept = default; }; struct derived : base {};
int main() { own::unique_owner<base> source; own::unique_owner<derived> target = std::move(source); }

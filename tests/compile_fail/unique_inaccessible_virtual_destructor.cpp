// expect: conversion from '[^']*unique_owner<derived(, ?(\(default\) )?(\[\.\.\.\]|(own::)?new_delete))?>
// expect: to (non-scalar type )?'(own::)?unique_owner<base(, ?(\(default\) )?(\[\.\.\.\]|(own::)?new_delete))?>'
#include <own/ownership.hpp>
#include <utility>

struct base { protected: virtual ~base() = default; }; struct derived : base { ~derived() override = default; };
int main() { auto source = own::make_unique<derived>(); own::unique_owner<base> target = std::move(source); }

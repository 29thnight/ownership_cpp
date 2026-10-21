// expect: conversion from '[^']*allocated_unique_owner<derived>
// expect: to (non-scalar type )?'(own::)?allocated_unique_owner<base>'
#include <own/ownership.hpp>
#include <memory>
#include <utility>

struct base { virtual ~base() noexcept = default; }; class derived : base {};
int main() { own::allocated_unique_owner<derived> source; own::allocated_unique_owner<base> target = std::move(source); }

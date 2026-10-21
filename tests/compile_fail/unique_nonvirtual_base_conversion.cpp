// expect: conversion from '[^']*unique_owner<derived>
// expect: to (non-scalar type )?'(own::)?unique_owner<base>'
#include <own/ownership.hpp>
#include <utility>

struct base {}; struct derived : base {};
int main() { auto source = own::make_unique<derived>(); own::unique_owner<base> target = std::move(source); }

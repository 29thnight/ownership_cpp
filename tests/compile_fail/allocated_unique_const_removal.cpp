// expect: conversion from '[^']*allocated_unique_owner<const int>
// expect: to (non-scalar type )?'(own::)?allocated_unique_owner<int>'
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto source = own::allocate_unique<const int>({}, 1); own::allocated_unique_owner<int> owner = std::move(source); }

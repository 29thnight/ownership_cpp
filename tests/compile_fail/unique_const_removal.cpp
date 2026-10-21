// expect: conversion from '[^']*unique_owner<const int(, ?(\(default\) )?(\[\.\.\.\]|(own::)?new_delete))?>
// expect: to (non-scalar type )?'(own::)?unique_owner<int(, ?(\(default\) )?(\[\.\.\.\]|(own::)?new_delete))?>'
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto source = own::make_unique<const int>(1); own::unique_owner<int> owner = std::move(source); }

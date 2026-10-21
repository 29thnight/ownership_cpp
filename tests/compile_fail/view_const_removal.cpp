// expect: conversion from '[^']*borrowed<const int>
// expect: to (non-scalar type )?'(own::)?borrowed<int>'
#include <own/ownership.hpp>
int main() { auto owner = own::make_shared<const int>(1); own::local_view<int> invalid = owner.borrow(); }

// expect: conversion from '(own::)?shared_owner<const int>' to (non-scalar type )?'(own::)?shared_owner<int>'
#include <own/ownership.hpp>
int main() { own::shared_owner<int> invalid = own::make_shared<const int>(1); }

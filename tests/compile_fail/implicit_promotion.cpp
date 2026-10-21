// expect: conversion from '(own::)?local_owner<int>' to (non-scalar type )?'(own::)?shared_owner<int>'
#include <own/ownership.hpp>
int main() { own::shared_owner<int> invalid = own::make_local<int>(1); }

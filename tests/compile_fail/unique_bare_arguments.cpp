// expect: no matching (function for call to|constructor for initialization of) '(own::)?unique_owner<std::(__cxx11::)?(basic_)?string
#include <own/ownership.hpp>
#include <string>

// Without std::in_place an argument list could read as adopting an object.
int main() { own::unique_owner<std::string> owner("text"); }

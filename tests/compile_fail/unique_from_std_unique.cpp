// expect: no matching (function for call to|constructor for initialization of) '(own::)?unique_owner<int(, ?(\(default\) )?(\[\.\.\.\]|(own::)?new_delete))?>
#include <own/ownership.hpp>
#include <memory>
#include <utility>

int main() { auto source = std::make_unique<int>(1); own::unique_owner<int> owner(std::move(source)); }

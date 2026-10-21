// expect: deleted.*\bunsafe_get\b
#include <own/ownership.hpp>
int main() { own::weak_owner<int> weak; (void)weak.lock().unsafe_get(); }

#include <own/ownership.hpp>
int main() { own::weak_owner<int> weak; (void)weak.lock().borrow(); }

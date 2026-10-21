#define BOUNDARY_BUILDING_MODULE
#include "boundary.hpp"

#if BOUNDARY_SHARED_PROVIDER
OWN_DEFINE_THREAD_ID_PROVIDER()
#endif

own::local_owner<int> make_in_module(int value) { return own::make_local<int>(value); }

// A local_owner created in one shared library / DLL and copied on the same
// thread in another binary. Built with hidden visibility on ELF so each binary
// keeps private copies of inline statics, as every DLL does on Windows.
#pragma once

#if defined(_WIN32)
#if defined(BOUNDARY_BUILDING_MODULE)
#define BOUNDARY_API __declspec(dllexport)
#else
#define BOUNDARY_API __declspec(dllimport)
#endif
#else
#define BOUNDARY_API __attribute__((visibility("default")))
#endif

// BOUNDARY_SHARED_PROVIDER=1: every binary asks the module for thread IDs.
// BOUNDARY_SHARED_PROVIDER=0: per-binary counters (the failure being fixed).
#if BOUNDARY_SHARED_PROVIDER
#define OWN_THREAD_ID_PROVIDER boundary_own_thread_id
#define OWN_THREAD_ID_PROVIDER_API BOUNDARY_API
#endif
#define OWN_DEBUG_THREAD_CHECK 1
#include <own/ownership.hpp>

BOUNDARY_API own::local_owner<int> make_in_module(int value);

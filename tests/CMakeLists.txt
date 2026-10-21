# CTest suite. Mirrors scripts/test.sh for every compiler CMake supports,
# including MSVC and clang-cl. Run: cmake -S . -B build && cmake --build build
# && ctest --test-dir build -C <config> --output-on-failure

# Executables and the module-boundary DLL share one directory so Windows finds it.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Threads REQUIRED)

if(MSVC)
    # C4324: an over-aligned test payload pads its control block, as intended.
    set(own_test_warnings /W4 /WX /permissive- /EHsc /wd4324)
else()
    set(own_test_warnings -Wall -Wextra -Wpedantic -Werror)
endif()

function(own_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEFINES;ARGS" ${ARGN})
    if(NOT ARG_SOURCES)
        set(ARG_SOURCES ${name}.cpp)
    endif()
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE own::ownership Threads::Threads)
    target_compile_options(${name} PRIVATE ${own_test_warnings})
    target_compile_definitions(${name} PRIVATE ${ARG_DEFINES})
    add_test(NAME ${name} COMMAND ${name} ${ARG_ARGS})
endfunction()

own_test(header_isolation)
own_test(multi_tu SOURCES multi_tu_a.cpp multi_tu_b.cpp multi_tu_main.cpp)
own_test(mixed_thread_check SOURCES mixed_thread_check_off.cpp mixed_thread_check_on.cpp mixed_thread_check_main.cpp)
own_test(ownership_tests)
own_test(borrow_tests)
own_test(owner_from_this_tests)
own_test(default_allocation_failure_tests)
own_test(fresh_hint_tests)
own_test(local_exclusive_tests)
own_test(null_comparison_tests)
own_test(unique_owner_tests)
own_test(allocated_unique_owner_tests)
own_test(unique_default_allocation_tests)
own_test(api_extensions_tests)
own_test(unique_allocator_tests)
own_test(layout_tests)
# Death tests for the debug thread checks; they use POSIX fork and report SKIP
# elsewhere. NDEBUG is undefined for this target so that Release builds still
# exercise the checks, in optimized code.
own_test(thread_confinement_tests DEFINES OWN_ENABLE_UNSAFE_GET_WARNING=0)
if(MSVC)
    target_compile_options(thread_confinement_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(thread_confinement_tests PRIVATE -UNDEBUG)
endif()

# Raw-pointer escape: with the warning disabled the sources build and run.
own_test(unsafe_get_contract DEFINES OWN_ENABLE_UNSAFE_GET_WARNING=0)
own_test(unique_unsafe_get_contract DEFINES OWN_ENABLE_UNSAFE_GET_WARNING=0)
own_test(allocated_unique_unsafe_get_contract DEFINES OWN_ENABLE_UNSAFE_GET_WARNING=0)
own_test(multi_tu_warning_0 SOURCES multi_tu_a.cpp multi_tu_b.cpp multi_tu_main.cpp
    DEFINES OWN_ENABLE_UNSAFE_GET_WARNING=0)

# OWN_FAIL_FAST gets the reason. The hook prints "reported" and exits 3; a
# plain abort (the hook bypassed) prints nothing and fails the regex.
add_executable(fail_hook_tests fail_hook_tests.cpp)
target_link_libraries(fail_hook_tests PRIVATE own::ownership Threads::Threads)
target_compile_options(fail_hook_tests PRIVATE ${own_test_warnings})
add_test(NAME fail_hook.none COMMAND fail_hook_tests none)
foreach(scenario invalid_allocator thread_mismatch)
    add_test(NAME fail_hook.${scenario} COMMAND fail_hook_tests ${scenario})
    set_tests_properties(fail_hook.${scenario} PROPERTIES PASS_REGULAR_EXPRESSION "^reported [0-9]+: own: ")
endforeach()

# A local_owner created in a shared library / DLL and copied in the executable
# on the same thread. With one thread-ID provider it passes; with per-binary
# counters (the control) the debug check aborts, which is the documented
# reason the provider exists.
foreach(provider 1 0)
    set(module boundary_module_${provider})
    add_library(${module} SHARED module_boundary/module.cpp)
    target_link_libraries(${module} PRIVATE own::ownership)
    target_compile_options(${module} PRIVATE ${own_test_warnings})
    target_compile_definitions(${module} PRIVATE BOUNDARY_SHARED_PROVIDER=${provider})
    add_executable(boundary_main_${provider} module_boundary/main.cpp)
    target_link_libraries(boundary_main_${provider} PRIVATE own::ownership ${module} Threads::Threads)
    target_compile_options(boundary_main_${provider} PRIVATE ${own_test_warnings})
    target_compile_definitions(boundary_main_${provider} PRIVATE BOUNDARY_SHARED_PROVIDER=${provider})
    set_target_properties(${module} boundary_main_${provider} PROPERTIES
        CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    add_test(NAME module_boundary.provider_${provider} COMMAND boundary_main_${provider})
endforeach()
set_tests_properties(module_boundary.provider_0 PROPERTIES PASS_REGULAR_EXPRESSION "ABORTED by the local_owner thread check")

# Compile-time rejections.
set(own_compile_fail_driver
    ${CMAKE_COMMAND}
    -DCOMPILER=${CMAKE_CXX_COMPILER}
    -DCOMPILER_ID=${CMAKE_CXX_COMPILER_ID}
    -DFRONTEND=${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}
    -DINCLUDE=${PROJECT_SOURCE_DIR}/include
    -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/compile_fail)
if(NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT)
    list(TRANSFORM own_compile_fail_driver REPLACE "^-DFRONTEND=$" "-DFRONTEND=GNU")
endif()

file(GLOB own_compile_fail_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/*.cpp)
foreach(source IN LISTS own_compile_fail_sources)
    get_filename_component(name ${source} NAME_WE)
    add_test(NAME compile_fail.${name}
        COMMAND ${own_compile_fail_driver} -DSOURCE=${source} -P ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail.cmake)
    set_tests_properties(compile_fail.${name} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP ${name}")
endforeach()

# unsafe_get() must warn with the exact message by default and with =1, which
# -Werror / /WX turns into a rejection. The message is ours, so it is checked
# with every compiler, including cl.exe.
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(own_undefine_warning /UOWN_ENABLE_UNSAFE_GET_WARNING)
    set(own_define_warning_1 /DOWN_ENABLE_UNSAFE_GET_WARNING=1)
    set(own_define_kind /DOWN_UNSAFE_GET_KIND=)
else()
    set(own_undefine_warning -UOWN_ENABLE_UNSAFE_GET_WARNING)
    set(own_define_warning_1 -DOWN_ENABLE_UNSAFE_GET_WARNING=1)
    set(own_define_kind -DOWN_UNSAFE_GET_KIND=)
endif()
set(own_warning_text "Borrowed raw pointer: caller must preserve lifetime; do not delete or otherwise deallocate the returned pointer")
foreach(setting default 1)
    if(setting STREQUAL "default")
        set(extra ${own_undefine_warning})
    else()
        set(extra ${own_undefine_warning} ${own_define_warning_1})
    endif()
    foreach(kind 0 1 2 3)
        add_test(NAME unsafe_get_warning.kind_${kind}_${setting}
            COMMAND ${own_compile_fail_driver} -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/unsafe_get_warning.cpp
                "-DEXTRA=${extra};${own_define_kind}${kind}" "-DEXPECT=${own_warning_text}" -DEXPECT_ON_MSVC=ON
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/unsafe_get_${kind}_${setting}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail.cmake)
    endforeach()
    foreach(source unique_unsafe_get_warning allocated_unique_unsafe_get_warning)
        add_test(NAME ${source}.${setting}
            COMMAND ${own_compile_fail_driver} -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/${source}.cpp
                "-DEXTRA=${extra}" "-DEXPECT=${own_warning_text}" -DEXPECT_ON_MSVC=ON
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/${source}_${setting}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail.cmake)
    endforeach()
endforeach()
foreach(kind 0 1 2 3)
    own_test(unsafe_get_warning_0_kind_${kind} SOURCES unsafe_get_warning.cpp
        DEFINES OWN_ENABLE_UNSAFE_GET_WARNING=0 OWN_UNSAFE_GET_KIND=${kind})
endforeach()
own_test(unique_unsafe_get_warning_0 SOURCES unique_unsafe_get_warning.cpp DEFINES OWN_ENABLE_UNSAFE_GET_WARNING=0)
own_test(allocated_unique_unsafe_get_warning_0 SOURCES allocated_unique_unsafe_get_warning.cpp
    DEFINES OWN_ENABLE_UNSAFE_GET_WARNING=0)

# On MSVC-style toolchains, settings that differ between translation units of
# one binary must fail to link (#pragma detect_mismatch in the header).
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    add_test(NAME detect_mismatch
        COMMAND ${CMAKE_COMMAND} -DCOMPILER=${CMAKE_CXX_COMPILER} -DINCLUDE=${PROJECT_SOURCE_DIR}/include
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/detect_mismatch
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/detect_mismatch
            -P ${CMAKE_CURRENT_SOURCE_DIR}/detect_mismatch.cmake)
endif()

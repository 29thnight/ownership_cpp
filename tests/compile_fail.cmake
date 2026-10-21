# Script mode (cmake -P): compiles one source that must be rejected.
#
# Inputs: COMPILER, COMPILER_ID (GNU/Clang/MSVC), FRONTEND (MSVC for cl and
# clang-cl, GNU otherwise), SOURCE, INCLUDE, OUTPUT_DIR, and optionally EXTRA
# (a ;-list of extra flags), EXPECT (a literal text that replaces the source's
# expectations) and EXPECT_ON_MSVC (check expectations even with cl.exe, whose
# wording differs; used only for messages the library itself defines).
#
# The source's "// expect: <ERE>" lines must each match the compiler output.
# They are written against GCC/Clang wording, so with cl.exe only rejection is
# checked unless EXPECT_ON_MSVC is set. clang-cl prints Clang's wording, so the
# expectations apply. "// only: clang|gcc" skips the case for other families.
# The authoritative check (POSIX grep -E, word boundaries included) is
# scripts/test.sh; CMake's regex has no \b, so it is dropped here.

foreach(required COMPILER COMPILER_ID FRONTEND SOURCE INCLUDE OUTPUT_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "compile_fail.cmake: ${required} is required")
    endif()
endforeach()

get_filename_component(name "${SOURCE}" NAME_WE)
file(STRINGS "${SOURCE}" directives REGEX "^// (expect|only): ")

if(COMPILER_ID STREQUAL "GNU")
    set(family gcc)
elseif(COMPILER_ID MATCHES "Clang")
    set(family clang)
else()
    set(family msvc)
endif()

set(expectations)
foreach(line IN LISTS directives)
    if(line MATCHES "^// only: (.*)$")
        if(NOT CMAKE_MATCH_1 STREQUAL family)
            message("SKIP ${name} (only: ${CMAKE_MATCH_1})")
            return()
        endif()
    elseif(line MATCHES "^// expect: (.*)$")
        list(APPEND expectations "${CMAKE_MATCH_1}")
    endif()
endforeach()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
# ASCII diagnostics: under a UTF-8 locale GCC uses typographic quotes.
set(ENV{LC_ALL} C)
set(ENV{LANG} C)
if(FRONTEND STREQUAL "MSVC")
    set(arguments /nologo /c /std:c++20 /EHsc /permissive- /W4 /WX /wd4189 /wd4100
        "/I${INCLUDE}" /DOWN_ENABLE_UNSAFE_GET_WARNING=0 "/Fo${OUTPUT_DIR}/${name}.obj")
    if(family STREQUAL "clang")
        list(APPEND arguments -Wno-unused-variable -Wno-unused-but-set-variable)
    endif()
else()
    set(arguments -std=c++20 -fsyntax-only -Wall -Wextra -Wpedantic -Werror
        -Wno-unused-variable -Wno-unused-but-set-variable
        "-I${INCLUDE}" -DOWN_ENABLE_UNSAFE_GET_WARNING=0)
endif()
# EXTRA comes last so it can override the warning-setting define.
execute_process(
    COMMAND "${COMPILER}" ${arguments} ${EXTRA} "${SOURCE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)

if(result EQUAL 0)
    message(FATAL_ERROR "${name} compiled, but it must be rejected")
endif()

if(family STREQUAL "msvc" AND NOT EXPECT_ON_MSVC)
    message("PASS ${name} rejected (cl.exe: reason not checked)")
    return()
endif()
if(NOT expectations AND NOT DEFINED EXPECT)
    message(FATAL_ERROR "${name} has no // expect: line")
endif()
if(DEFINED EXPECT)
    string(FIND "${output}" "${EXPECT}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "${name} was rejected for another reason; missing text: ${EXPECT}\n${output}")
    endif()
else()
    foreach(pattern IN LISTS expectations)
        string(REPLACE "\\b" "" cmake_pattern "${pattern}")
        if(NOT output MATCHES "${cmake_pattern}")
            message(FATAL_ERROR "${name} was rejected for another reason; missing: ${pattern}\n${output}")
        endif()
    endforeach()
endif()
message("PASS ${name} rejected for its stated reason")

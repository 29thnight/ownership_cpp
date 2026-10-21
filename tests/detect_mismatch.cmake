# Script mode (cmake -P), MSVC-style frontends only (cl.exe, clang-cl).
# Builds a.cpp and b.cpp into one executable, first with the same settings
# (must link) and then with OWN_ENABLE_UNSAFE_GET_WARNING differing between
# them (must fail with LNK2038 from the header's #pragma detect_mismatch).
# Inputs: COMPILER, INCLUDE, SOURCE_DIR, OUTPUT_DIR.
foreach(required COMPILER INCLUDE SOURCE_DIR OUTPUT_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "detect_mismatch.cmake: ${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(common /nologo /EHsc /std:c++20 /permissive- "/I${INCLUDE}")

function(build label setting_a setting_b result_var output_var)
    foreach(part a b)
        set(setting ${setting_${part}})
        execute_process(
            COMMAND "${COMPILER}" ${common} /c /DOWN_ENABLE_UNSAFE_GET_WARNING=${setting}
                "${SOURCE_DIR}/${part}.cpp" "/Fo${OUTPUT_DIR}/${label}_${part}.obj"
            RESULT_VARIABLE compiled OUTPUT_VARIABLE log ERROR_VARIABLE log)
        if(NOT compiled EQUAL 0)
            message(FATAL_ERROR "${label}: ${part}.cpp did not compile\n${log}")
        endif()
    endforeach()
    execute_process(
        COMMAND "${COMPILER}" /nologo "${OUTPUT_DIR}/${label}_a.obj" "${OUTPUT_DIR}/${label}_b.obj"
            "/Fe${OUTPUT_DIR}/${label}.exe"
        RESULT_VARIABLE linked OUTPUT_VARIABLE log ERROR_VARIABLE log)
    set(${result_var} ${linked} PARENT_SCOPE)
    set(${output_var} "${log}" PARENT_SCOPE)
endfunction()

build(consistent 0 0 linked log)
if(NOT linked EQUAL 0)
    message(FATAL_ERROR "consistent settings failed to link\n${log}")
endif()
build(mismatched 0 1 linked log)
if(linked EQUAL 0)
    message(FATAL_ERROR "mismatched OWN_ENABLE_UNSAFE_GET_WARNING linked; detect_mismatch had no effect")
endif()
if(NOT log MATCHES "LNK2038|own_enable_unsafe_get_warning")
    message(FATAL_ERROR "mismatched settings failed to link for another reason\n${log}")
endif()
message("PASS consistent settings link; a mismatch fails with LNK2038")

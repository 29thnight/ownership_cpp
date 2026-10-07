#!/usr/bin/env bash
# Self-contained GCC/Clang C++20 test runner; no downloaded test framework.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cxx=${CXX:-g++}
build_root=${OWN_TEST_BUILD_DIR:-"$root/build/tests"}
mode=${1:-default}

if [[ "$mode" == "--help" || "$mode" == "-h" ]]; then
    echo 'Usage: scripts/test.sh [default|debug|release|asan|ubsan|tsan|all]'
    echo 'default runs debug and release. all also requests each sanitizer.'
    echo 'CXX, CXXFLAGS, and OWN_TEST_BUILD_DIR may be overridden.'
    echo 'A requested sanitizer failure is reported as a failure, never silently skipped.'
    exit 0
fi

run_mode() {
    local selected=$1
    local output="$build_root/$selected"
    local -a flags=(-std=c++20 -Wall -Wextra -Wpedantic -Werror -pthread -I"$root/include")
    case "$selected" in
        debug) flags+=(-O0 -g) ;;
        release) flags+=(-O3 -DNDEBUG) ;;
        asan) flags+=(-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined) ;;
        ubsan) flags+=(-O1 -g -fno-omit-frame-pointer -fsanitize=undefined -fno-sanitize-recover=all) ;;
        tsan) flags+=(-O1 -g -fno-omit-frame-pointer -fsanitize=thread) ;;
        *) echo "Unknown test mode: $selected" >&2; return 2 ;;
    esac
    if [[ -n "${CXXFLAGS:-}" ]]; then
        local -a extra_flags
        read -r -a extra_flags <<< "$CXXFLAGS"
        flags+=("${extra_flags[@]}")
    fi
    mkdir -p "$output"
    printf '\n=== %s (%s) ===\n' "$selected" "$cxx"
    "$cxx" "${flags[@]}" "$root/tests/header_isolation.cpp" -o "$output/header_isolation"
    "$output/header_isolation"
    "$cxx" "${flags[@]}" "$root/tests/multi_tu_a.cpp" "$root/tests/multi_tu_b.cpp" \
        "$root/tests/multi_tu_main.cpp" -o "$output/multi_tu"
    "$output/multi_tu"
    # Translation units with and without debug thread checks share local groups.
    "$cxx" "${flags[@]}" "$root/tests/mixed_thread_check_off.cpp" "$root/tests/mixed_thread_check_on.cpp" \
        "$root/tests/mixed_thread_check_main.cpp" -o "$output/mixed_thread_check"
    "$output/mixed_thread_check"
    "$cxx" "${flags[@]}" "$root/tests/ownership_tests.cpp" -o "$output/ownership_tests"
    "$output/ownership_tests"
    "$cxx" "${flags[@]}" "$root/tests/borrow_tests.cpp" -o "$output/borrow_tests"
    "$output/borrow_tests"
    "$cxx" "${flags[@]}" "$root/tests/owner_from_this_tests.cpp" -o "$output/owner_from_this_tests"
    "$output/owner_from_this_tests"
    "$cxx" "${flags[@]}" "$root/tests/default_allocation_failure_tests.cpp" -o "$output/default_allocation_failure_tests"
    "$output/default_allocation_failure_tests"
    "$cxx" "${flags[@]}" "$root/tests/fresh_hint_tests.cpp" -o "$output/fresh_hint_tests"
    "$output/fresh_hint_tests"
    "$cxx" "${flags[@]}" "$root/tests/local_exclusive_tests.cpp" -o "$output/local_exclusive_tests"
    "$output/local_exclusive_tests"
    "$cxx" "${flags[@]}" "$root/tests/null_comparison_tests.cpp" -o "$output/null_comparison_tests"
    "$output/null_comparison_tests"
    "$cxx" "${flags[@]}" "$root/tests/unique_owner_tests.cpp" -o "$output/unique_owner_tests"
    "$output/unique_owner_tests"
    "$cxx" "${flags[@]}" "$root/tests/allocated_unique_owner_tests.cpp" -o "$output/allocated_unique_owner_tests"
    "$output/allocated_unique_owner_tests"
    "$cxx" "${flags[@]}" "$root/tests/unique_default_allocation_tests.cpp" -o "$output/unique_default_allocation_tests"
    "$output/unique_default_allocation_tests"

    # Death tests intentionally abort. Run them without a sanitizer so the
    # signal result tests the contract rather than sanitizer signal handling.
    if [[ "$selected" == debug ]]; then
        "$cxx" "${flags[@]}" -UOWN_ENABLE_UNSAFE_GET_WARNING -DOWN_ENABLE_UNSAFE_GET_WARNING=0 "$root/tests/thread_confinement_tests.cpp" -o "$output/thread_confinement_tests"
        "$output/thread_confinement_tests"
        # Intentional raw-pointer escape diagnostics are tested separately:
        # normal header/owner/view operators above must remain warning-clean.
        local kind setting name source
        local warning='Borrowed raw pointer: caller must preserve lifetime; do not delete or otherwise deallocate the returned pointer'
        for kind in 0 1 2 3; do
            for setting in default 1; do
                local -a warning_flags=(-UOWN_ENABLE_UNSAFE_GET_WARNING)
                if [[ "$setting" != default ]]; then
                    warning_flags+=(-DOWN_ENABLE_UNSAFE_GET_WARNING="$setting")
                fi
                name="unsafe_get_kind_${kind}_warning_${setting}"
                if "$cxx" "${flags[@]}" "${warning_flags[@]}" -DOWN_UNSAFE_GET_KIND="$kind" \
                    -c "$root/tests/unsafe_get_warning.cpp" -o "$output/$name.o" >"$output/$name.log" 2>&1; then
                    echo "FAIL expected unsafe_get warning: $name" >&2
                    return 1
                fi
                if ! grep -Fq "$warning" "$output/$name.log" || ! grep -q deprecated "$output/$name.log"; then
                    echo "FAIL missing exact unsafe_get deprecation diagnostic: $name" >&2
                    cat "$output/$name.log" >&2
                    return 1
                fi
                echo "PASS exact unsafe_get warning: $name"
            done
            name="unsafe_get_kind_${kind}_warning_0"
            "$cxx" "${flags[@]}" -UOWN_ENABLE_UNSAFE_GET_WARNING -DOWN_ENABLE_UNSAFE_GET_WARNING=0 \
                -DOWN_UNSAFE_GET_KIND="$kind" "$root/tests/unsafe_get_warning.cpp" -o "$output/$name"
            "$output/$name"
            echo "PASS disabled unsafe_get warning: $name"
        done
        for source in unique_unsafe_get_warning allocated_unique_unsafe_get_warning; do
            for setting in default 1; do
                local -a unique_warning_flags=(-UOWN_ENABLE_UNSAFE_GET_WARNING)
                if [[ "$setting" != default ]]; then
                    unique_warning_flags+=(-DOWN_ENABLE_UNSAFE_GET_WARNING="$setting")
                fi
                name="${source}_${setting}"
                if "$cxx" "${flags[@]}" "${unique_warning_flags[@]}" \
                    -c "$root/tests/$source.cpp" -o "$output/$name.o" >"$output/$name.log" 2>&1; then
                    echo "FAIL expected unique unsafe_get warning: $name" >&2
                    return 1
                fi
                if ! grep -Fq "$warning" "$output/$name.log" || ! grep -q deprecated "$output/$name.log"; then
                    echo "FAIL missing exact unique unsafe_get deprecation diagnostic: $name" >&2
                    cat "$output/$name.log" >&2
                    return 1
                fi
                echo "PASS exact unique unsafe_get warning: $name"
            done
        done
        for source in unique_unsafe_get_warning unique_unsafe_get_contract allocated_unique_unsafe_get_warning allocated_unique_unsafe_get_contract; do
            "$cxx" "${flags[@]}" -UOWN_ENABLE_UNSAFE_GET_WARNING -DOWN_ENABLE_UNSAFE_GET_WARNING=0 \
                "$root/tests/$source.cpp" -o "$output/${source}_0"
            "$output/${source}_0"
            echo "PASS disabled unique unsafe_get warning and contract: $source"
        done
        "$cxx" "${flags[@]}" -UOWN_ENABLE_UNSAFE_GET_WARNING -DOWN_ENABLE_UNSAFE_GET_WARNING=0 \
            "$root/tests/unsafe_get_contract.cpp" -o "$output/unsafe_get_contract"
        "$output/unsafe_get_contract"
        # Consistent program-wide =0 configuration also links across TUs.
        "$cxx" "${flags[@]}" -UOWN_ENABLE_UNSAFE_GET_WARNING -DOWN_ENABLE_UNSAFE_GET_WARNING=0 \
            "$root/tests/multi_tu_a.cpp" "$root/tests/multi_tu_b.cpp" \
            "$root/tests/multi_tu_main.cpp" -o "$output/multi_tu_warning_0"
        "$output/multi_tu_warning_0"
        echo "PASS unsafe_get contract and warning-disabled multi-TU link"
        for source in "$root"/tests/compile_fail/*.cpp; do
            name=$(basename "$source" .cpp)
            if "$cxx" "${flags[@]}" -UOWN_ENABLE_UNSAFE_GET_WARNING -DOWN_ENABLE_UNSAFE_GET_WARNING=0 -Wno-unused-variable -Wno-unused-but-set-variable -c "$source" -o "$output/$name.o" >"$output/$name.log" 2>&1; then
                echo "FAIL compile rejection: $name unexpectedly compiled" >&2
                return 1
            fi
            echo "PASS compile rejection: $name"
        done
    fi
    echo "PASS $selected (including isolated header and multi-translation-unit link)"
}

case "$mode" in
    default)
        run_mode debug
        run_mode release
        ;;
    all)
        # Run modes in separate invocations so a failing sanitizer does not
        # suppress the remaining checks and every failed stage stays visible.
        failed=0
        for selected in debug release asan ubsan tsan; do
            if "$0" "$selected"; then :; else
                echo "FAIL mode: $selected (see compiler/runtime output above)" >&2
                failed=1
            fi
        done
        exit "$failed"
        ;;
    *) run_mode "$mode" ;;
esac

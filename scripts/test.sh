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
    "$cxx" "${flags[@]}" "$root/tests/ownership_tests.cpp" -o "$output/ownership_tests"
    "$output/ownership_tests"
    "$cxx" "${flags[@]}" "$root/tests/default_allocation_failure_tests.cpp" -o "$output/default_allocation_failure_tests"
    "$output/default_allocation_failure_tests"

    # Death tests intentionally abort. Run them without a sanitizer so the
    # signal result tests the contract rather than sanitizer signal handling.
    if [[ "$selected" == debug ]]; then
        "$cxx" "${flags[@]}" "$root/tests/thread_confinement_tests.cpp" -o "$output/thread_confinement_tests"
        "$output/thread_confinement_tests"
        local source name
        for source in "$root"/tests/compile_fail/*.cpp; do
            name=$(basename "$source" .cpp)
            if "$cxx" "${flags[@]}" -c "$source" -o "$output/$name.o" >"$output/$name.log" 2>&1; then
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

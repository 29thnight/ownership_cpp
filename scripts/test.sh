#!/usr/bin/env bash
# Self-contained GCC/Clang C++20 test runner; no downloaded test framework.
set -euo pipefail

# Compiler diagnostics in plain ASCII: under a UTF-8 locale GCC quotes names
# with typographic quotes, which the compile-fail expectations do not use.
export LC_ALL=C

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
        tsan)
            flags+=(-O1 -g -fno-omit-frame-pointer -fsanitize=thread)
            # Clang links its TSan runtime statically by default, and that
            # archive defines operator new/delete, so the tests that replace
            # the global allocation functions fail to link with duplicate
            # symbols. The shared runtime lets the test's definitions win.
            if "$cxx" --version 2>/dev/null | grep -qi clang; then
                flags+=(-shared-libsan -Wl,-rpath,"$("$cxx" -print-runtime-dir)")
            fi
            ;;
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
    "$cxx" "${flags[@]}" "$root/tests/api_extensions_tests.cpp" -o "$output/api_extensions_tests"
    "$output/api_extensions_tests"
    "$cxx" "${flags[@]}" "$root/tests/unique_allocator_tests.cpp" -o "$output/unique_allocator_tests"
    "$output/unique_allocator_tests"
    "$cxx" "${flags[@]}" "$root/tests/sized_deallocation_tests.cpp" -o "$output/sized_deallocation_tests"
    "$output/sized_deallocation_tests"
    "$cxx" "${flags[@]}" "$root/tests/layout_tests.cpp" -o "$output/layout_tests"
    "$output/layout_tests"

    # OWN_FAIL_FAST receives the reason; the hook exits with 3 (an abort means it was bypassed).
    "$cxx" "${flags[@]}" "$root/tests/fail_hook_tests.cpp" -o "$output/fail_hook_tests"
    "$output/fail_hook_tests" none
    local scenario status
    for scenario in invalid_allocator thread_mismatch; do
        set +e
        "$output/fail_hook_tests" "$scenario" >"$output/fail_hook_$scenario.log" 2>&1
        status=$?
        set -e
        if (( status != 3 )) || ! grep -q "reported" "$output/fail_hook_$scenario.log"; then
            echo "FAIL fail hook: $scenario exited $status" >&2
            cat "$output/fail_hook_$scenario.log" >&2
            return 1
        fi
        echo "PASS fail hook: $scenario -> $(cat "$output/fail_hook_$scenario.log")"
    done

    # A local_owner made in a shared library and copied on the same thread in the
    # executable. Hidden visibility gives each binary private inline statics, as
    # every DLL has on Windows. Sanitizer runtimes do not mix well with this
    # extra shared object, so it runs in the plain modes.
    if [[ "$selected" == debug || "$selected" == release ]]; then
        local boundary="$root/tests/module_boundary" provider
        for provider in 1 0; do
            local dir="$output/module_boundary_$provider"
            mkdir -p "$dir"
            "$cxx" "${flags[@]}" -DBOUNDARY_SHARED_PROVIDER="$provider" -fPIC -shared \
                -fvisibility=hidden -fvisibility-inlines-hidden "$boundary/module.cpp" -o "$dir/libboundary.so"
            "$cxx" "${flags[@]}" -DBOUNDARY_SHARED_PROVIDER="$provider" \
                -fvisibility=hidden -fvisibility-inlines-hidden "$boundary/main.cpp" -L"$dir" -lboundary \
                -Wl,-rpath,"$dir" -o "$dir/main"
            set +e
            "$dir/main" >"$dir/run.log" 2>&1
            status=$?
            set -e
            if [[ "$provider" == 1 && "$status" != 0 ]]; then
                echo "FAIL module boundary with a shared thread-ID provider (exit $status)" >&2
                cat "$dir/run.log" >&2
                return 1
            fi
            if [[ "$provider" == 0 ]] && ! grep -q "ABORTED by the local_owner thread check" "$dir/run.log"; then
                echo "FAIL module boundary control: per-binary IDs were expected to abort (exit $status)" >&2
                cat "$dir/run.log" >&2
                return 1
            fi
        done
        echo "PASS module boundary: shared provider passes; per-binary control aborts as documented"
    fi

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
        # Each case must be rejected for its stated reason: every "// expect: <ERE>"
        # line must match the compiler output. "// only: clang" limits a case to
        # one compiler family (for example Clang's lifetime analysis).
        local family=gcc
        if "$cxx" --version 2>/dev/null | grep -qi clang; then family=clang; fi
        for source in "$root"/tests/compile_fail/*.cpp; do
            name=$(basename "$source" .cpp)
            local only
            only=$(sed -n 's#^// only: ##p' "$source")
            if [[ -n "$only" && "$only" != "$family" ]]; then
                echo "SKIP compile rejection: $name (only: $only)"
                continue
            fi
            if "$cxx" "${flags[@]}" -UOWN_ENABLE_UNSAFE_GET_WARNING -DOWN_ENABLE_UNSAFE_GET_WARNING=0 -Wno-unused-variable -Wno-unused-but-set-variable -c "$source" -o "$output/$name.o" >"$output/$name.log" 2>&1; then
                echo "FAIL compile rejection: $name unexpectedly compiled" >&2
                return 1
            fi
            if grep -Eq "fatal error: .*(No such file or directory|file not found)" "$output/$name.log"; then
                echo "FAIL compile rejection: $name could not find a header; the compiler environment is broken" >&2
                cat "$output/$name.log" >&2
                return 1
            fi
            local expectations=0 pattern
            while IFS= read -r pattern; do
                expectations=$((expectations + 1))
                if ! grep -Eq -- "$pattern" "$output/$name.log"; then
                    echo "FAIL compile rejection: $name was rejected for another reason; missing: $pattern" >&2
                    cat "$output/$name.log" >&2
                    return 1
                fi
            done < <(sed -n 's#^// expect: ##p' "$source")
            if (( expectations == 0 )); then
                echo "FAIL compile rejection: $name has no // expect: line" >&2
                return 1
            fi
            echo "PASS compile rejection: $name"
        done
    fi
    echo "PASS $selected (including isolated header and multi-translation-unit link)"
}

# Several modes run in separate invocations, so one failing mode never hides
# the result of the others; every failed mode is listed at the end.
run_modes() {
    local failed=() selected
    for selected in "$@"; do
        if "$0" "$selected"; then :; else
            failed+=("$selected")
        fi
    done
    if (( ${#failed[@]} )); then
        echo "FAIL modes: ${failed[*]} (see compiler/runtime output above)" >&2
        exit 1
    fi
}

case "$mode" in
    default)
        run_modes debug release
        ;;
    all)
        run_modes debug release asan ubsan tsan
        ;;
    *) run_mode "$mode" ;;
esac

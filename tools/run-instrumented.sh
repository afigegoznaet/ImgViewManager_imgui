#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir="$project_dir/build/instrumented"
if ! command -v gdb >/dev/null; then
    printf 'GDB is required to capture shutdown backtraces.\n' >&2
    exit 1
fi
cmake -S "$project_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Debug -DIVM_ENABLE_SANITIZERS=ON
cmake --build "$build_dir" --parallel 4
mkdir -p "$build_dir/crash-logs"
log_file="$build_dir/crash-logs/$(date -u +%Y%m%dT%H%M%SZ)-$$.log"
export ASAN_OPTIONS=detect_leaks=0:abort_on_error=1:symbolize=1
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
printf 'Reproduce the slow shutdown, then send this log: %s\n' "$log_file"
gdb --quiet --nx --batch \
    -x "$project_dir/tools/shutdown-watch.gdb" \
    --args "$build_dir/imgui_ivm" "$@" 2>&1 | tee "$log_file"

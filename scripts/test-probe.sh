#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$repo_dir/.build"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic \
    -I "$repo_dir/third_party/enshwick" "$repo_dir/tests/probe_test.c" \
    -o "$repo_dir/.build/probe_test"
"$repo_dir/.build/probe_test"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic \
    -I "$repo_dir/third_party/enshwick" "$repo_dir/tests/diagnostics_test.c" \
    -o "$repo_dir/.build/diagnostics_test"
"$repo_dir/.build/diagnostics_test"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -pthread \
    -I "$repo_dir/third_party/enshwick" "$repo_dir/tests/cost_trace_test.c" \
    -o "$repo_dir/.build/cost_trace_test"
"$repo_dir/.build/cost_trace_test"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
    "$repo_dir/tests/sprint_stub_test.c" -o "$repo_dir/.build/sprint_stub_test"
"$repo_dir/.build/sprint_stub_test"

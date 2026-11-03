#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$repo_dir/.build"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
    -I "$repo_dir/include" "$repo_dir/tests/policy_test.cpp" \
    -o "$repo_dir/.build/policy_test"
"$repo_dir/.build/policy_test"

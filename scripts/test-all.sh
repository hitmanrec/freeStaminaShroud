#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sh "$repo_dir/scripts/test.sh"
sh "$repo_dir/scripts/test-probe.sh"
python3 -m unittest discover -s "$repo_dir/tests" -p 'test_*.py' -v

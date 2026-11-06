#!/usr/bin/env bash
set -euo pipefail
test_dir=$(cd "$(dirname "$0")" && pwd)
out=${1:-/tmp/ffamc-eos-test}
mkdir -p "$out"
flags=()
if [[ ${SANITIZE:-0} == 1 ]]; then flags=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
for threaded in 0 1; do
    for crop in 0 1; do
        binary="$out/ffamc_eos_test_${threaded}_${crop}"
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror "${flags[@]}" \
            -DTEST_THREADED="$threaded" -DTEST_CROP="$crop" \
            "$test_dir/ffamc_eos_test.c" -o "$binary"
        "$binary"
    done
done

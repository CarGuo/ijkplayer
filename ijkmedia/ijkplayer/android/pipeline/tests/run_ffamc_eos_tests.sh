#!/usr/bin/env bash
set -euo pipefail
test_dir=$(cd "$(dirname "$0")" && pwd)
out=${1:-/tmp/ffamc-eos-test}
mkdir -p "$out"
flags=()
if [[ ${SANITIZE:-0} == 1 ]]; then flags=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror "${flags[@]}" "$test_dir/ffamc_eos_test.c" -o "$out/ffamc_eos_test"
"$out/ffamc_eos_test"

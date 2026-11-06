#!/usr/bin/env bash
# Host regression only. No network, Android SDK, device, or fixture generator.
set -euo pipefail
TEST_DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$TEST_DIR/../../.." && pwd)
FFSRC=${1:-${FFMPEG_SOURCE_DIR:-}}
FFBUILD=${2:-${FFMPEG_BUILD_DIR:-}}
if [[ -z "$FFSRC" || -z "$FFBUILD" || $# -gt 3 ]]; then
    echo "Usage: $0 FFMPEG_SOURCE_DIR FFMPEG_BUILD_DIR [OUTPUT_DIR]" >&2
    echo "The first two arguments may instead be supplied as environment variables." >&2
    exit 2
fi
FFSRC=$(cd "$FFSRC" && pwd)
FFBUILD=$(cd "$FFBUILD" && pwd)
for input in "$FFSRC/libavcodec/avcodec.h" "$FFSRC/libavformat/avformat.h" \
             "$FFBUILD/config.h" "$FFBUILD/libavcodec/libavcodec.a" "$FFBUILD/libavutil/libavutil.a"; do
    [[ -r "$input" ]] || { echo "Missing FFmpeg host input: $input" >&2; exit 2; }
done
OUT=${3:-${OUTPUT_DIR:-}}
if [[ -z "$OUT" ]]; then OUT=$(mktemp -d "${TMPDIR:-/tmp}/ijk-h264-crop.XXXXXX"); fi
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
case "$OUT/" in "$TEST_DIR/"*) echo "Keep generated output outside the test source directory." >&2; exit 2;; esac
CC=${CC:-cc}
MODES=${MODES:-"normal sanitized"}
for mode in $MODES; do
    case "$mode" in normal|sanitized) ;; *) echo "Unknown MODES value: $mode" >&2; exit 2;; esac
done
python3 "$TEST_DIR/verify_inputs.py" "$OUT/inputs.json" "$FFSRC" "$FFBUILD"
python3 "$TEST_DIR/extract_helpers.py" "$OUT/generated"
FIX="$TEST_DIR/fixtures"
for mode in $MODES; do
    flags=()
    if [[ "$mode" == sanitized ]]; then flags=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
    mkdir -p "$OUT/$mode"
    common=(-std=c11 -O1 -g -Wall -Wextra -Werror -Wno-unused-function -I"$OUT/generated")
    for test in crop_parameters generation feeder baseline_epoch resolution_coexist independent_core; do
        "$CC" "${common[@]}" -Wno-sign-compare "${flags[@]}" -I"$FFBUILD" -I"$FFSRC" \
            "$TEST_DIR/${test}_test.c" "$FFBUILD/libavcodec/libavcodec.a" \
            "$FFBUILD/libavutil/libavutil.a" -lm -pthread -lz -o "$OUT/$mode/$test"
    done
    for test in flag_before_marker_input lifecycle; do
        "$CC" "${common[@]}" "${flags[@]}" "$TEST_DIR/${test}_test.c" -o "$OUT/$mode/$test"
    done
    "$CC" "${common[@]}" "${flags[@]}" "$OUT/generated/eligibility_test.c" -o "$OUT/$mode/eligibility"
    export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0:abort_on_error=1}
    export UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1}
    # Each suite gets a separate log. Any compiler or test failure stops the run.
    {
        for fixture in actual small; do
            "$OUT/$mode/crop_parameters" "$FIX/${fixture}_a.h264" "$FIX/${fixture}_b.h264"
        done
        "$OUT/$mode/generation" "$FIX/actual_a.h264" "$FIX/actual_b.h264"
        "$OUT/$mode/feeder"
        "$OUT/$mode/flag_before_marker_input"
        "$OUT/$mode/lifecycle"
    } > "$OUT/$mode/original.log" 2> "$OUT/$mode/original.stderr"
    "$OUT/$mode/baseline_epoch" "$FIX/actual_a.h264" "$FIX/actual_b.h264" \
        "$FIX/avc3/first_A.avcc-au" "$FIX/avc3/boundary_B.avcc-au" \
        "$FIX/ordinary_video_packets.bin" > "$OUT/$mode/epoch.log" 2> "$OUT/$mode/epoch.stderr"
    {
        "$OUT/$mode/resolution_coexist" "$FIX/actual_a.h264" "$FIX/actual_b.h264" \
            "$FIX/avc3/first_A.avcc-au" "$FIX/avc3/boundary_B.avcc-au" \
            "$FIX/resolution/C_480x272.avcc" "$FIX/resolution/first_C_480x272.avcc-au"
        "$OUT/$mode/eligibility"
    } > "$OUT/$mode/coexistence.log" 2> "$OUT/$mode/coexistence.stderr"
    "$OUT/$mode/independent_core" "$FIX/actual_a.h264" "$FIX/actual_b.h264" \
        > "$OUT/$mode/independent.log" 2> "$OUT/$mode/independent.stderr"
    for suite in original epoch coexistence independent; do
        cat "$OUT/$mode/$suite.log"
    done
done
python3 "$TEST_DIR/verify_inputs.py" "$OUT/inputs-after.json" "$FFSRC" "$FFBUILD"
cmp "$OUT/inputs.json" "$OUT/inputs-after.json"
echo "PASS selected host suites completed; output: $OUT"

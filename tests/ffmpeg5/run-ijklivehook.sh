#!/usr/bin/env bash
# Deterministic production wrapper with mocked inner I/O, real outer FFmpeg API.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SOURCE=$(cd "${1:?FFmpeg source}" && pwd)
BUILD=$(cd "${2:?FFmpeg host build}" && pwd)
OUTPUT=${3:?output directory}
mkdir -p "$OUTPUT"
FLAGS=(-std=gnu11 -D_GNU_SOURCE -g -O2 -Werror=implicit-function-declaration
 -Werror=incompatible-pointer-types -Wno-deprecated-declarations
 -I"$BUILD" -I"$SOURCE" -I"$ROOT/ijkmedia")
if [[ -n ${SANITIZE:-} ]]; then FLAGS+=(-fsanitize="$SANITIZE" -fno-omit-frame-pointer); fi
"${CC:-cc}" "${FLAGS[@]}" "$ROOT/tests/ffmpeg5/test_ijklivehook.c" \
 -Wl,--start-group "$BUILD"/{libavformat/libavformat.a,libavcodec/libavcodec.a,libavutil/libavutil.a} \
 -Wl,--end-group -lm -lpthread -lz -latomic -o "$OUTPUT/test-ijklivehook"
timeout 20 "$OUTPUT/test-ijklivehook"
# Explicit in-process-only validation; never starts the loopback fixture.
if [[ ${IJKLIVEHOOK_LOCAL_ONLY:-0} == 1 ]]; then exit 0; fi

"${CC:-cc}" "${FLAGS[@]}" "$ROOT/tests/ffmpeg5/test_ijklivehook_network.c" \
 -Wl,--start-group "$BUILD"/{libavformat/libavformat.a,libavcodec/libavcodec.a,libavutil/libavutil.a} \
 -Wl,--end-group -lm -lpthread -lz -latomic -o "$OUTPUT/test-ijklivehook-network"
PYTHONDONTWRITEBYTECODE=1 python3 "$ROOT/tests/ffmpeg5/rtsp-live/run_wrapper_network.py" "$OUTPUT/test-ijklivehook-network"

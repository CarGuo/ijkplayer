#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SOURCE=$(cd "${1:?FFmpeg source}" && pwd)
BUILD=$(cd "${2:?FFmpeg host build}" && pwd)
OUTPUT=${3:?output directory}
mkdir -p "$OUTPUT/include/libffmpeg"
OUTPUT=$(cd "$OUTPUT" && pwd)
ln -sfn "$BUILD/config.h" "$OUTPUT/include/libffmpeg/config.h"
ln -sfn "$BUILD/config_components.h" "$OUTPUT/include/config_components.h"
printf '#define IJKPLAYER_VERSION "ffmpeg5-rtsp-test"\n' > "$OUTPUT/include/ijkversion.h"
FLAGS=(-std=gnu11 -D_GNU_SOURCE -g -O3 -ffast-math -fno-fast-math
 -ffunction-sections -fdata-sections -Werror=implicit-function-declaration
 -Werror=incompatible-pointer-types -Wno-deprecated-declarations
 -I"$OUTPUT/include" -I"$BUILD" -I"$SOURCE" -I"$ROOT/ijkmedia" -I"$ROOT/ijkmedia/ijkplayer")
if [[ -n ${SANITIZE:-} ]]; then FLAGS+=(-fsanitize="$SANITIZE" -fno-omit-frame-pointer); fi
"${CC:-cc}" "${FLAGS[@]}" "$ROOT/tests/ffmpeg5/test_rtsp_live.c" \
 "$ROOT/ijkmedia/ijkplayer/ijkavformat/ijklivehook.c" \
 "$ROOT/ijkmedia/ijksdl/ijksdl_audio.c" "$ROOT/ijkmedia/ijksdl/ijksdl_vout.c" \
 "$ROOT/ijkmedia/ijksdl/ijksdl_mutex.c" "$ROOT/ijkmedia/ijksdl/ijksdl_error.c" "$ROOT/ijkmedia/ijksdl/ijksdl_aout.c" \
 -Wl,--gc-sections -Wl,--start-group "$BUILD"/{libavformat/libavformat.a,libavcodec/libavcodec.a,libavutil/libavutil.a,libswscale/libswscale.a,libswresample/libswresample.a} \
 -Wl,--end-group -lm -lpthread -lz -latomic -o "$OUTPUT/test-rtsp-live"
"$OUTPUT/test-rtsp-live"

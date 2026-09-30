#!/usr/bin/env bash
# Usage: tests/ffmpeg5/run-host.sh FFMPEG_SOURCE FFMPEG_BUILD [OUTPUT_DIRECTORY]
# The FFmpeg build must include png encoder/decoder, h264/pcm_s16le decoders,
# mov/wav demuxers, file protocol, swscale and zlib. Set SANITIZE=address,undefined
# to instrument the IJK translation units (FFmpeg must be rebuilt separately
# with sanitizer flags for fully instrumented third-party library coverage).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SOURCE=$(cd "${1:?provide the patched FFmpeg source directory}" && pwd)
BUILD=$(cd "${2:?provide its configured native build directory}" && pwd)
OUTPUT=${3:-"$ROOT/../build/ijk-core-tests"}
mkdir -p "$OUTPUT"
OUTPUT=$(cd "$OUTPUT" && pwd)
mkdir -p "$OUTPUT/include/libffmpeg" "$OUTPUT/fixtures"
ln -sfn "$BUILD/config.h" "$OUTPUT/include/libffmpeg/config.h"
ln -sfn "$BUILD/config_components.h" "$OUTPUT/include/config_components.h"
printf '#define IJKPLAYER_VERSION "ffmpeg5-host-test"\n' > "$OUTPUT/include/ijkversion.h"
CC=${CC:-cc}
# Match the Android APP optimization followed by the ijkplayer module override.
# CLOCK_CFLAGS='-O3 -ffast-math' deliberately reproduces broken NaN handling.
read -r -a CLOCK_FLAGS <<< "${CLOCK_CFLAGS:--O3 -ffast-math -fno-fast-math}"
FLAGS=(-std=gnu11 -D_GNU_SOURCE -g "${CLOCK_FLAGS[@]}" -ffunction-sections -fdata-sections
       -Werror=implicit-function-declaration -Werror=incompatible-pointer-types
       -Werror=discarded-qualifiers -Wno-deprecated-declarations
       -I"$OUTPUT/include" -I"$BUILD" -I"$SOURCE" -I"$ROOT/ijkmedia" -I"$ROOT/ijkmedia/ijkplayer")
if [[ -n ${SANITIZE:-} ]]; then
    FLAGS+=(-fsanitize="$SANITIZE" -fno-omit-frame-pointer)
fi
LIBS=(-Wl,--gc-sections -Wl,--start-group
      "$BUILD/libavformat/libavformat.a" "$BUILD/libavcodec/libavcodec.a"
      "$BUILD/libavutil/libavutil.a" "$BUILD/libswscale/libswscale.a"
      "$BUILD/libswresample/libswresample.a" -Wl,--end-group -lm -lpthread -lz)
SDL=("$ROOT/ijkmedia/ijksdl/ijksdl_mutex.c" "$ROOT/ijkmedia/ijksdl/ijksdl_error.c")
for file in "$ROOT/ijkmedia/ijkplayer/ff_ffplay.c" "$ROOT/ijkmedia/ijkplayer/ff_cmdutils.c" \
            "$ROOT/ijkmedia/ijkplayer/ijkmeta.c" "$ROOT/ijkmedia/ijkplayer/ijkavformat/"*.c \
            "$ROOT/ijkmedia/ijkplayer/ijkavutil/ijkdict.c"; do
    case "$file" in *ijkmediadatasource.c|*ijkioandroidio.c) continue ;; esac
    "$CC" "${FLAGS[@]}" -fsyntax-only "$file"
done
echo "PASS strict host API compile (JNI-only files excluded)"
"$CC" "${FLAGS[@]}" "$ROOT/tests/ffmpeg5/test_core.c" "${SDL[@]}" \
    "$ROOT/ijkmedia/ijksdl/ijksdl_aout.c" "${LIBS[@]}" -o "$OUTPUT/test-core"
"$CC" "${FLAGS[@]}" "$ROOT/tests/ffmpeg5/test_las_io.c" "${SDL[@]}" \
    "$ROOT/ijkmedia/ijksdl/ijksdl_thread.c" "$ROOT/ijkmedia/ijkplayer/ijkavformat/cJSON.c" \
    "${LIBS[@]}" -o "$OUTPUT/test-las-io"
"$CC" "${FLAGS[@]}" "$ROOT/tests/ffmpeg5/test_dict.c" \
    "$ROOT/ijkmedia/ijkplayer/ijkavutil/ijkdict.c" "$ROOT/ijkmedia/ijkplayer/ijkavutil/ijkutils.c" \
    -o "$OUTPUT/test-dict"
# Generate deterministic small input fixtures with an installed host ffmpeg.
"${FFMPEG:-ffmpeg}" -hide_banner -loglevel error -f lavfi -i testsrc2=size=64x32:rate=12 \
    -frames:v 12 -c:v libx264 -bf 3 -g 12 -pix_fmt yuv420p -y "$OUTPUT/fixtures/bframes.mp4"
"${FFMPEG:-ffmpeg}" -hide_banner -loglevel error -f lavfi -i sine=frequency=440:sample_rate=8000 \
    -t 0.2 -c:a pcm_s16le -y "$OUTPUT/fixtures/las.wav"
"$OUTPUT/test-core" "$OUTPUT/fixtures" "$OUTPUT/fixtures/bframes.mp4"
"$OUTPUT/test-las-io" "$OUTPUT/fixtures/las.wav"
"$OUTPUT/test-dict"

#!/usr/bin/env bash
set -euo pipefail
# Usage: run_ffamc_ffmpeg_tests.sh FFMPEG_SOURCE FFMPEG_HOST_BUILD [OUTPUT_DIR]
# HOST_BUILD must contain static 5.1.10 avformat/avcodec/avutil with mov/file,
# H264/HEVC decoders, parsers, and both mp4toannexb BSFs enabled.
source_dir=$(cd "$1" && pwd)
build_dir=$(cd "$2" && pwd)
for feature in CONFIG_H264_DECODER CONFIG_HEVC_DECODER CONFIG_H264_MP4TOANNEXB_BSF CONFIG_HEVC_MP4TOANNEXB_BSF; do
    grep -qx "#define $feature 1" "$build_dir/config_components.h" || { echo "Host test prerequisite missing: $feature" >&2; exit 2; }
done
output_dir=${3:-"$build_dir/mediacodec-tests"}
mkdir -p "$output_dir"
output_dir=$(cd "$output_dir" && pwd)
test_dir=$(cd "$(dirname "$0")" && pwd)
ffmpeg_bin=${FFMPEG_BIN:-ffmpeg}
cc_bin=${CC:-cc}
# System ffmpeg is only used to generate fixtures. Tests link the supplied
# FFmpeg 5.1.10 static libraries, never the system libraries.
for spec in h264:64x48:5 width:96x48:3 height:64x96:3; do
    IFS=: read -r name size frames <<< "$spec"
    "$ffmpeg_bin" -hide_banner -loglevel error -f lavfi -i "color=c=red:s=$size:r=5" \
        -frames:v "$frames" -c:v libx264 -pix_fmt yuv420p -g 5 -bf 2 -y "$output_dir/$name.mp4"
done
"$ffmpeg_bin" -hide_banner -loglevel error -f lavfi -i color=c=red:s=64x48:r=5 \
    -frames:v 3 -c:v libx265 -x265-params 'pools=none:frame-threads=1:log-level=error' \
    -pix_fmt yuv420p -y "$output_dir/hevc.mp4"
# Set SANITIZE=1 for ASan/UBSan on the test translation units. A fully
# instrumented FFmpeg build gives stronger coverage of the codec libraries.
sanitize=()
if [[ ${SANITIZE:-0} == 1 ]]; then
    sanitize=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
for name in ffamc_ffmpeg ffamc_packet_queue ffamc_input; do
    "$cc_bin" -std=c11 -Wall -Wextra -Werror -Wno-sign-compare "${sanitize[@]}" \
        -I"$build_dir" -I"$source_dir" "$test_dir/${name}_test.c" \
        "$build_dir/libavformat/libavformat.a" "$build_dir/libavcodec/libavcodec.a" \
        "$build_dir/libavutil/libavutil.a" -lm -pthread -lz -o "$output_dir/${name}_test"
done
"$output_dir/ffamc_ffmpeg_test" "$output_dir/h264.mp4" "$output_dir/hevc.mp4" \
    "$output_dir/width.mp4" "$output_dir/height.mp4"
"$output_dir/ffamc_packet_queue_test"

"$output_dir/ffamc_input_test" "$output_dir/h264.mp4" "$output_dir/hevc.mp4"

#!/usr/bin/env bash
# Pinned-source out-of-tree FFmpeg 5 build using the NDK LLVM toolchain.
# FFMPEG5_SOURCE must be the ijk-patched n5.1.10 checkout, not vanilla FFmpeg.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCH=${1:?usage: compile-ffmpeg5.sh arm64|armv7a|x86_64}
: "${ANDROID_NDK:?set ANDROID_NDK}"
: "${FFMPEG5_SOURCE:?set FFMPEG5_SOURCE to the patched source}"
: "${OPENSSL_SOURCE:?set OPENSSL_SOURCE to OpenSSL_1_1_1w source (baseline)}"
# Keep ffbuild/version.sh at the tested version string after committing patches.
# Source identity is recorded separately; this does not guarantee identical bytes.
export revision=n5.1.10
JOBS=${JOBS:-4}
case $(uname -s) in Linux) HOST=linux-x86_64;; Darwin) HOST=darwin-x86_64;; *) exit 2;; esac
BIN="$ANDROID_NDK/toolchains/llvm/prebuilt/$HOST/bin"
export PATH="$BIN:$PATH"
case "$ARCH" in
 arm64) TARGET=aarch64-linux-android; API=21; FARCH=aarch64; SSL=android-arm64; EXTRA='-Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384';;
 armv7a) TARGET=armv7a-linux-androideabi; API=16; FARCH=arm; SSL=android-arm; EXTRA='';;
 x86_64) TARGET=x86_64-linux-android; API=21; FARCH=x86_64; SSL=android-x86_64; EXTRA='-Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384';;
 *) echo "Unsupported ABI: $ARCH" >&2; exit 2;;
esac
CC="$BIN/${TARGET}${API}-clang"
BUILD="$ROOT/android/contrib/build/ffmpeg-$ARCH"
OUT="$BUILD/output"
SSLBUILD="$ROOT/android/contrib/build/openssl-$ARCH/obj5"
SSLOUT="$ROOT/android/contrib/build/openssl-$ARCH/output"
mkdir -p "$BUILD/obj5" "$OUT" "$SSLBUILD" "$SSLOUT"
# Preserve the existing TLS baseline; do not silently lose HTTPS support.
if [[ ! -f "$SSLOUT/lib/libssl.a" ]]; then
 (
  cd "$SSLBUILD"
  export ANDROID_NDK_HOME="$ANDROID_NDK"
  perl "$OPENSSL_SOURCE/Configure" "$SSL" -D__ANDROID_API__="$API" no-shared no-tests --prefix="$SSLOUT" --openssldir="$SSLOUT/ssl"
  make -j"$JOBS"
  make install_sw
 )
fi
PROFILE="$ROOT/config/module-lite-more.sh"
source "$PROFILE"
FLAGS=()
for flag in $COMMON_FF_CFG_FLAGS; do
 case "$flag" in --disable-avresample) ;; *) FLAGS+=("$flag");; esac
done
cd "$BUILD/obj5"
# Always reconfigure FFmpeg. OpenSSL archives are cached separately above;
# use a fresh TLS build directory if its source, toolchain or flags change.
"$FFMPEG5_SOURCE/configure" "${FLAGS[@]}" \
 --prefix="$OUT" --arch="$FARCH" --target-os=android --enable-cross-compile \
 --cc="$CC" --cxx="$BIN/${TARGET}${API}-clang++" --ld="$CC" \
 --ar="$BIN/llvm-ar" --nm="$BIN/llvm-nm" --ranlib="$BIN/llvm-ranlib" --strip="$BIN/llvm-strip" \
 --enable-static --disable-shared --enable-pic --enable-openssl \
 --extra-cflags="-O2 -fPIC -DANDROID -I$SSLOUT/include" \
 --extra-ldflags="-L$SSLOUT/lib $EXTRA" --extra-libs='-ldl' \
 --enable-bsf=h264_mp4toannexb --enable-bsf=hevc_mp4toannexb \
 --enable-demuxer=ijklivehook --enable-demuxer=ijklas
for required in CONFIG_H264_MP4TOANNEXB_BSF CONFIG_HEVC_MP4TOANNEXB_BSF CONFIG_IJKLIVEHOOK_DEMUXER CONFIG_IJKLAS_DEMUXER CONFIG_HTTPS_PROTOCOL CONFIG_MJPEG_PARSER CONFIG_MPJPEG_DEMUXER; do
 grep -qx "#define $required 1" config_components.h || { echo "Missing required feature: $required" >&2; exit 1; }
done
make -j"$JOBS"
make install
LIBS=(libavfilter/libavfilter.a libavformat/libavformat.a libavcodec/libavcodec.a libswresample/libswresample.a libswscale/libswscale.a libavutil/libavutil.a)
"$CC" -shared -Wl,-Bsymbolic -Wl,-soname,libijkffmpeg.so -Wl,--no-undefined -Wl,-z,noexecstack $EXTRA \
 -Wl,--whole-archive "${LIBS[@]}" -Wl,--no-whole-archive \
 "$SSLOUT/lib/libssl.a" "$SSLOUT/lib/libcrypto.a" -lm -lz -ldl -latomic -o "$OUT/libijkffmpeg.so"
# ijk custom protocols use private FFmpeg APIs; headers must match this exact build.
for lib in libavutil libavformat libavcodec; do
 cp "$FFMPEG5_SOURCE/$lib/"*.h "$OUT/include/$lib/"
done
mkdir -p "$OUT/include/libffmpeg"
cp config.h "$OUT/include/libffmpeg/config.h"
cp config.h "$OUT/include/config.h"
cp config_components.h "$OUT/include/config_components.h"
{
 echo "ffmpeg_source=$(git -C "$FFMPEG5_SOURCE" rev-parse HEAD)"
 echo "ffmpeg_revision=$revision"
 echo "ffmpeg_version=$(sed -n 's/^#define FFMPEG_VERSION "\(.*\)"$/\1/p' libavutil/ffversion.h)"
 if [[ -n $(git -C "$FFMPEG5_SOURCE" status --porcelain --untracked-files=normal) ]]; then
  echo 'ffmpeg_source_dirty=true'
 else
  echo 'ffmpeg_source_dirty=false'
 fi
 for patch_name in 0001-ijk-hooks.patch 0002-mpjpeg-eof.patch; do
  echo "patch_${patch_name}_sha256=$(sha256sum "$ROOT/patches/ffmpeg5/$patch_name" | cut -d' ' -f1)"
 done
 echo "ffmpeg_config_sha256=$(sha256sum config.h config_components.h | sha256sum | cut -d' ' -f1)"
 echo "profile=module-lite-more.sh"
 echo "profile_sha256=$(sha256sum "$PROFILE" | cut -d' ' -f1)"
 echo "ndk=$(grep Pkg.Revision "$ANDROID_NDK/source.properties")"
 echo "arch=$ARCH api=$API"
 sha256sum "$OUT/libijkffmpeg.so"
} > "$OUT/build-manifest.txt"
echo "Built $OUT/libijkffmpeg.so"

#!/usr/bin/env bash
# Pinned-source out-of-tree FFmpeg 5 build using the NDK LLVM toolchain.
# FFMPEG5_SOURCE must be the ijk-patched n5.1.10 checkout, not vanilla FFmpeg.
# OPENSSL_ONLY=1 builds/verifies only the same TLS cache used by the normal build,
# writes <TLS-output>/build-manifest.txt, and never reads FFMPEG5_SOURCE.
# Example: OPENSSL_ONLY=1 android/contrib/compile-ffmpeg5.sh arm64
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCH=${1:?usage: compile-ffmpeg5.sh arm64|armv7a|x86_64}
: "${ANDROID_NDK:?set ANDROID_NDK}"
OPENSSL_ONLY=${OPENSSL_ONLY:-0}
case "$OPENSSL_ONLY" in 0|1) ;; *) echo 'OPENSSL_ONLY must be 0 or 1' >&2; exit 2;; esac
if [[ "$OPENSSL_ONLY" == 0 ]]; then
 : "${FFMPEG5_SOURCE:?set FFMPEG5_SOURCE to the patched source}"
fi
: "${OPENSSL_SOURCE:?set OPENSSL_SOURCE to the pinned OpenSSL 3.5.9 source}"
# Keep ffbuild/version.sh at the tested version string after committing patches.
# Source identity is recorded separately; this does not guarantee identical bytes.
export revision=n5.1.10
JOBS=${JOBS:-4}
# Resolve paths before entering out-of-tree build directories.
ANDROID_NDK=$(cd "$ANDROID_NDK" && pwd -P)
OPENSSL_SOURCE=$(cd "$OPENSSL_SOURCE" && pwd -P)
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
# Never consult the old openssl-$ARCH/{obj5,output} cache: it may be 1.1.1w.
SSL_VERSION=3.5.9
# Expected upstream provenance is distinct from the actual input tree hash/HEAD;
# OPENSSL_SOURCE may be a verified release archive or a locally modified checkout.
SSL_EXPECTED_UPSTREAM_COMMIT=45e844fa2a14ec92d146bd8f5778ac130b6625fb
SSL_EXPECTED_RELEASE_SHA256=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
for expected in MAJOR=3 MINOR=5 PATCH=9 PRE_RELEASE_TAG=; do
 grep -qx "$expected" "$OPENSSL_SOURCE/VERSION.dat" || {
  echo "Expected release OpenSSL $SSL_VERSION in OPENSSL_SOURCE; no build performed" >&2; exit 1;
 }
done

# Hash actual source/toolchain bytes, not just a tag, directory name or mtime.
# This also invalidates the cache for local edits and changed NDK sysroots.
# Perl and its core modules are already required by OpenSSL Configure.
hash_tree() {
 perl -MDigest::SHA -MFile::Find -e '
  chdir $ARGV[0] or die "chdir: $!";
  my @paths;
  find({ no_chdir => 1, wanted => sub {
   if ($_ eq "./.git") { $File::Find::prune = 1; return; }
   push @paths, $_ if -f $_ || -l $_;
  }}, ".");
  my $sha = Digest::SHA->new(256);
  for my $path (sort @paths) {
   $sha->add($path, "\0");
   if (-l $path) { $sha->add("link\0", readlink($path), "\0"); }
   else {
    open my $file, "<", $path or die "$path: $!";
    binmode $file;
    $sha->add("file\0", Digest::SHA->new(256)->addfile($file)->hexdigest, "\0");
    close $file;
   }
  }
  print $sha->hexdigest, "\n";
 ' "$1"
}
SSL_SOURCE_SHA256=$(hash_tree "$OPENSSL_SOURCE")
SSL_SOURCE_GIT_HEAD=not-a-git-checkout
SSL_SOURCE_GIT_DIRTY=not-applicable
if [[ -e "$OPENSSL_SOURCE/.git" ]]; then
 SSL_SOURCE_GIT_HEAD=$(git -C "$OPENSSL_SOURCE" rev-parse HEAD)
 if [[ -n $(git -C "$OPENSSL_SOURCE" status --porcelain --untracked-files=normal) ]]; then
  SSL_SOURCE_GIT_DIRTY=true
 else
  SSL_SOURCE_GIT_DIRTY=false
 fi
fi
SSL_TOOLCHAIN_SHA256=$({
 sha256sum "$ANDROID_NDK/source.properties"
 hash_tree "${BIN%/bin}"
} | sha256sum | cut -d' ' -f1)
SSL_FLAGS=("$SSL" "-D__ANDROID_API__=$API" no-shared no-module no-dso no-apps no-tests -fPIC --libdir=lib)
SSL_IDENTITY=$({
 echo 'cache_schema=openssl-static-v1'
 echo "openssl_version=$SSL_VERSION"
 echo "openssl_expected_upstream_commit=$SSL_EXPECTED_UPSTREAM_COMMIT"
 echo "openssl_expected_release_tarball_sha256=$SSL_EXPECTED_RELEASE_SHA256"
 echo "cache_root=$ROOT/android/contrib/build/openssl-$ARCH/ffmpeg5-$SSL_VERSION"
 echo "openssl_source=$OPENSSL_SOURCE"
 echo "openssl_source_tree_sha256=$SSL_SOURCE_SHA256"
 echo "openssl_source_git_head=$SSL_SOURCE_GIT_HEAD"
 echo "openssl_source_git_dirty=$SSL_SOURCE_GIT_DIRTY"
 echo "android_ndk=$ANDROID_NDK"
 echo "android_toolchain_tree_sha256=$SSL_TOOLCHAIN_SHA256"
 echo "arch=$ARCH api=$API host=$HOST"
 printf 'configure_flags='; printf '%q ' "${SSL_FLAGS[@]}"; printf '\n'
 # Configure/make run with a clean environment below. Bind host generators too.
 echo "path=$PATH"
 echo "home=${HOME:-/}"
 sha256sum "$(command -v perl)" "$(command -v make)"
 perl -V:version -V:archname
})
SSL_CACHE_KEY=$(printf '%s\n' "$SSL_IDENTITY" | sha256sum | cut -d' ' -f1)
SSLCACHE="$ROOT/android/contrib/build/openssl-$ARCH/ffmpeg5-$SSL_VERSION/$SSL_CACHE_KEY"
SSLBUILD="$SSLCACHE/obj"
SSLOUT="$SSLCACHE/output"
ssl_cache_valid() {
 [[ -s "$SSLOUT/lib/libssl.a" && -s "$SSLOUT/lib/libcrypto.a" &&
    -f "$SSLOUT/.identity" && -f "$SSLOUT/.artifacts.sha256" ]] &&
 [[ $(cat "$SSLOUT/.identity") == "$SSL_IDENTITY" ]] &&
 grep -qx '# define OPENSSL_VERSION_STR "3.5.9"' "$SSLOUT/include/openssl/opensslv.h" &&
 (cd "$SSLOUT" && sha256sum -c .artifacts.sha256 >/dev/null 2>&1)
}
# Clear inherited compiler/config/make overrides so they cannot poison a cache
# built with the same recorded options. No dynamically loaded providers are used.
ssl_run() {
 env -i PATH="$PATH" HOME="${HOME:-/}" LC_ALL=C ANDROID_NDK_ROOT="$ANDROID_NDK" "$@"
}
if ssl_cache_valid; then
 echo "Using OpenSSL $SSL_VERSION cache $SSL_CACHE_KEY"
else
 (
  mkdir -p "$SSLCACHE"
  mkdir "$SSLCACHE/.lock" 2>/dev/null || {
   echo "OpenSSL cache is locked: $SSLCACHE/.lock (check for an active build)" >&2; exit 1;
  }
  trap 'rmdir "$SSLCACHE/.lock"' EXIT
  # Another process could have finished while we calculated the identity.
  if ssl_cache_valid; then exit 0; fi
  # Only this content-addressed cache is rebuilt; baseline output stays intact.
  rm -rf "$SSLBUILD" "$SSLOUT"
  mkdir -p "$SSLBUILD" "$SSLOUT"
  cd "$SSLBUILD"
  ssl_run perl "$OPENSSL_SOURCE/Configure" "${SSL_FLAGS[@]}" --prefix="$SSLOUT" --openssldir="$SSLOUT/ssl"
  ssl_run make -j"$JOBS"
  ssl_run make install_sw
  [[ -s "$SSLOUT/lib/libssl.a" && -s "$SSLOUT/lib/libcrypto.a" ]]
  grep -qx '# define OPENSSL_VERSION_STR "3.5.9"' "$SSLOUT/include/openssl/opensslv.h"
  # A cache is reusable only after installation completed and all headers and
  # archives were recorded. A partial build or modified archive fails closed.
  cd "$SSLOUT"
  { sha256sum lib/libssl.a lib/libcrypto.a; find include -type f -exec sha256sum {} +; } > .artifacts.sha256
  printf '%s\n' "$SSL_IDENTITY" > .identity
 )
fi
# This manifest describes only the TLS dependency, even when called as part of a
# full FFmpeg build. OPENSSL_ONLY is deliberately absent from the cache identity.
{
 echo 'artifact=openssl-static'
 printf '%s\n' "$SSL_IDENTITY"
 echo "openssl_cache_key=$SSL_CACHE_KEY"
 echo "openssl_output=$SSLOUT"
 sha256sum "$SSLOUT/lib/libssl.a" "$SSLOUT/lib/libcrypto.a"
} > "$SSLOUT/build-manifest.txt.tmp.$$"
mv "$SSLOUT/build-manifest.txt.tmp.$$" "$SSLOUT/build-manifest.txt"
echo "OpenSSL $SSL_VERSION ready: $SSLOUT"
echo "OpenSSL manifest: $SSLOUT/build-manifest.txt"
if [[ "$OPENSSL_ONLY" == 1 ]]; then exit 0; fi
FFMPEG5_SOURCE=$(cd "$FFMPEG5_SOURCE" && pwd -P)
mkdir -p "$BUILD/obj5" "$OUT"
PROFILE="$ROOT/config/module-lite-more.sh"
source "$PROFILE"
FLAGS=()
for flag in $COMMON_FF_CFG_FLAGS; do
 case "$flag" in --disable-avresample) ;; *) FLAGS+=("$flag");; esac
done
cd "$BUILD/obj5"
# Always reconfigure FFmpeg against the verified, content-addressed TLS cache.
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
 for patch_name in 0001-ijk-hooks.patch 0002-mpjpeg-eof.patch 0003-hevc-parameter-updates.patch 0004-tls-peer-identity.patch; do
  echo "patch_${patch_name}_sha256=$(sha256sum "$ROOT/patches/ffmpeg5/$patch_name" | cut -d' ' -f1)"
 done
 echo "ffmpeg_config_sha256=$(sha256sum config.h config_components.h | sha256sum | cut -d' ' -f1)"
 echo "profile=module-lite-more.sh"
 echo "profile_sha256=$(sha256sum "$PROFILE" | cut -d' ' -f1)"
 echo "ndk=$(grep Pkg.Revision "$ANDROID_NDK/source.properties")"
 echo "arch=$ARCH api=$API"
 echo "openssl_version=$SSL_VERSION"
 echo "openssl_expected_upstream_commit=$SSL_EXPECTED_UPSTREAM_COMMIT"
 echo "openssl_expected_release_tarball_sha256=$SSL_EXPECTED_RELEASE_SHA256"
 echo "openssl_source_tree_sha256=$SSL_SOURCE_SHA256"
 echo "openssl_source_git_head=$SSL_SOURCE_GIT_HEAD"
 echo "openssl_source_git_dirty=$SSL_SOURCE_GIT_DIRTY"
 echo "openssl_toolchain_tree_sha256=$SSL_TOOLCHAIN_SHA256"
 echo "openssl_cache_key=$SSL_CACHE_KEY"
 echo "openssl_output=$SSLOUT"
 printf 'openssl_configure_flags='; printf '%q ' "${SSL_FLAGS[@]}"; printf '\n'
 sha256sum "$SSLOUT/lib/libssl.a" "$SSLOUT/lib/libcrypto.a"
 sha256sum "$OUT/libijkffmpeg.so"
} > "$OUT/build-manifest.txt"
echo "Built $OUT/libijkffmpeg.so"

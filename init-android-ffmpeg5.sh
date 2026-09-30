#!/usr/bin/env bash
# Prepare pinned sources for the additive FFmpeg 5 Android build path.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")" && pwd)
cd "$ROOT"
checkout() {
 local url=$1 dir=$2 ref=$3 expected=$4
 if [[ ! -d "$dir/.git" ]]; then
  if [[ -e "$dir" ]]; then echo "Refusing to overwrite $dir" >&2; exit 1; fi
  git clone --depth 1 --branch "$ref" "$url" "$dir"
 fi
 [[ $(git -C "$dir" rev-parse HEAD) == "$expected" ]] || {
  echo "Unexpected source revision in $dir; preserving it unchanged" >&2; exit 1;
 }
}
# The branch is the discovery ref; the immutable commit is the source identity.
# This fork commit already contains both patches. Never apply them a second time.
checkout https://github.com/CarGuo/FFmpeg.git extra/ffmpeg5 ffmpeg-5.0 4a62cc600696982f189fbad6c6ea1593e909b814
for patch_name in 0001-ijk-hooks.patch 0002-mpjpeg-eof.patch; do
PATCH="$ROOT/patches/ffmpeg5/$patch_name"
if git -C extra/ffmpeg5 apply --reverse --check "$PATCH"; then
 echo "Exact patch $patch_name is already applied"
else
 echo "Expected patch $patch_name is missing or modified; no patch, reset or overwrite performed" >&2
 exit 1
fi
done
checkout https://github.com/openssl/openssl.git extra/openssl5 OpenSSL_1_1_1w e04bd3433fd84e1861bf258ea37928d9845e6a86
checkout https://github.com/Bilibili/libyuv.git ijkmedia/ijkyuv ijk-r0.2.1-dev a0c7dd3e4b095649881f39152655437b8cde1589
checkout https://github.com/Bilibili/soundtouch.git ijkmedia/ijksoundtouch ijk-r0.1.2-dev 6bf39cd3bf6b0c156267d12446b0d6bdcfcd53c2
printf '\nSources ready. Set ANDROID_NDK and run:\n'
printf 'export FFMPEG5_SOURCE="%s/extra/ffmpeg5"\n' "$ROOT"
printf 'export OPENSSL_SOURCE="%s/extra/openssl5"\n' "$ROOT"
printf 'android/contrib/compile-ffmpeg5.sh arm64\n(cd android && ./compile-ijk.sh arm64)\n'

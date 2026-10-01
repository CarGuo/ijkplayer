#!/usr/bin/env bash
# Prepare pinned sources for the additive FFmpeg 5 Android build path.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")" && pwd)
cd "$ROOT"
checkout() {
 local url=$1 dir=$2 ref=$3 expected=$4
 if [[ ! -d "$dir/.git" ]]; then
  if [[ -e "$dir" ]]; then echo "Refusing to overwrite $dir" >&2; exit 1; fi
  # Resolve the moving discovery ref only for a new directory. Branch README
  # updates must not change the immutable source used by this build path.
  git clone --depth 1 --no-checkout --branch "$ref" "$url" "$dir"
  if ! git -C "$dir" cat-file -e "$expected^{commit}" 2>/dev/null; then
   git -C "$dir" fetch --depth 1 origin "$expected"
  fi
  git -C "$dir" checkout --detach "$expected"
 fi
 [[ $(git -C "$dir" rev-parse HEAD) == "$expected" ]] || {
  echo "Unexpected source revision in $dir; preserving it unchanged" >&2; exit 1;
 }
}
# The branch is the discovery ref; the immutable commit is the source identity.
# This fork commit already contains all four patches. Never apply them a second time.
checkout https://github.com/CarGuo/FFmpeg.git extra/ffmpeg5 ffmpeg-5.0 5d01026835e0fc18be967461df180432e85db0c7
for patch_name in 0001-ijk-hooks.patch 0002-mpjpeg-eof.patch 0003-hevc-parameter-updates.patch 0004-tls-peer-identity.patch; do
PATCH="$ROOT/patches/ffmpeg5/$patch_name"
if git -C extra/ffmpeg5 apply --reverse --check "$PATCH"; then
 echo "Exact patch $patch_name is already applied"
else
 echo "Expected patch $patch_name is missing or modified; no patch, reset or overwrite performed" >&2
 exit 1
fi
done
# Use a new directory so existing extra/openssl5 (1.1.1w) stays unchanged.
# Official release archive SHA256: 603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
checkout https://github.com/openssl/openssl.git extra/openssl-3.5.9 openssl-3.5.9 45e844fa2a14ec92d146bd8f5778ac130b6625fb
checkout https://github.com/Bilibili/libyuv.git ijkmedia/ijkyuv ijk-r0.2.1-dev a0c7dd3e4b095649881f39152655437b8cde1589
checkout https://github.com/Bilibili/soundtouch.git ijkmedia/ijksoundtouch ijk-r0.1.2-dev 6bf39cd3bf6b0c156267d12446b0d6bdcfcd53c2
printf '\nSources ready. Set ANDROID_NDK and run:\n'
printf 'export FFMPEG5_SOURCE="%s/extra/ffmpeg5"\n' "$ROOT"
printf 'export OPENSSL_SOURCE="%s/extra/openssl-3.5.9"\n' "$ROOT"
printf 'android/contrib/compile-ffmpeg5.sh arm64\n(cd android && ./compile-ijk.sh arm64)\n'

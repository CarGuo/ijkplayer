# FFmpeg 5 Android migration candidate

The `ffmpeg-5.0` branch names the migration series; its actual FFmpeg baseline is
**5.1.10**. This is an additive Android build path. The old `init-android.sh`
continues to select the FFmpeg 4.3 fork; use the new initializer explicitly.

## Source identity and companion branches

- IJK starting point: `c92e1e6496c5a479ec93b7b22bf7aa36f1faf31e`.
- [Patched FFmpeg branch](https://github.com/CarGuo/FFmpeg/tree/ffmpeg-5.0):
  `4a62cc600696982f189fbad6c6ea1593e909b814`, based on upstream `n5.1.10`
  (`19feb712f5c1821d8a3fa1ad63c5bd2e3b9672eb`).
- [GSY companion branch and packaged-library notes](https://github.com/CarGuo/GSYVideoPlayer/blob/ffmpeg-5.0/doc/ffmpeg-5.0.md).
- OpenSSL remains `OpenSSL_1_1_1w`, commit
  `e04bd3433fd84e1861bf258ea37928d9845e6a86`.

The initializer clones the named FFmpeg fork branch, verifies the exact commit,
and reverse-checks the two patches already included in it. Existing unexpected
revisions or missing/modified patches cause a failure without resetting or
patching the checkout. If upgrading an older locally patched upstream checkout,
preserve it under another name before initializing `extra/ffmpeg5` again.
[Standalone patches](../patches/ffmpeg5/README.md) remain available for inspection
and upstream-source reconstruction. These checks verify revisions and patch
hunks, not all local changes; start from clean source trees. Unrelated edits
in FFmpeg or its dependencies can otherwise remain present.

## Build

The tested toolchain was Linux x86_64 with Android NDK r22b (`22.1.7171670`).
The new path requires Bash, Git, Perl, GNU Make and `sha256sum`, plus the NDK
and NASM (or YASM) for x86_64 assembly.
Run from this repository's root in a separate checkout/build tree from FFmpeg 4:

```sh
./init-android-ffmpeg5.sh
export ANDROID_NDK=/absolute/path/to/android-ndk-r22b
export FFMPEG5_SOURCE="$PWD/extra/ffmpeg5"
export OPENSSL_SOURCE="$PWD/extra/openssl5"
for abi in arm64 armv7a x86_64; do
  android/contrib/compile-ffmpeg5.sh "$abi"
  (cd android && ./compile-ijk.sh "$abi")
done
```

This explicitly uses `config/module-lite-more.sh`, including the MJPEG parser,
MPJPEG demuxer, IJK demuxers, HTTPS and H.264/HEVC Annex-B bitstream filters.
API levels are 21 for ARM64/x86_64 and 16 for ARMv7. The build outputs share the
legacy `android/contrib/build` destinations; do not mix previously built
FFmpeg 4 and FFmpeg 5 artifacts. Keep each ABI's `libijkffmpeg.so`,
`libijkplayer.so` and `libijksdl.so` together. OpenSSL static archives are cached;
use fresh build directories when changing its source, the NDK or build flags.
No native binaries are committed
to this IJK branch.

`compile-ffmpeg5.sh` exports `revision=n5.1.10` so FFmpeg's generated version
string stays at the tested value after the patches become a fork commit.
`android/contrib/build/ffmpeg-<abi>/output/build-manifest.txt` records the source
commit, dirty state, version string, patches, profile, configuration, toolchain
and output hash. Pinning source and version alone does **not** guarantee a
byte-identical rebuild; compiler, dependencies, paths and build environment also
matter. Check rebuilt hashes and rerun the relevant runtime tests.

## Optional reproduction of the tested candidate

Normal IJK builds keep the existing Git-derived player version. The frozen
candidate libraries were built from the **modified** IJK working tree based on
`c92e1e6496c5a479ec93b7b22bf7aa36f1faf31e`, before those migration changes were
committed. Their embedded `IJKPLAYER_VERSION` is therefore `c92e1e6`; this does
not mean they were built from the unmodified baseline commit.

Once the migration is committed, Git-derived version generation changes the
header even when the runtime source files are identical. The version string can
move read-only data and its references, changing `.text`, relocation
or data sections and the ELF build ID as well. The observed difference is not
limited to a metadata section. A real three-ABI NDK rebuild, changing only this
version override, reproduced **all nine** frozen library hashes in the original
build environment. The FFmpeg and SDL libraries already matched before it.

For that explicit candidate-reproduction use case, after building FFmpeg as
above, scope the override to each IJK invocation and record the actual source
commit separately:

```sh
mkdir -p android/contrib/build
printf 'ijk_source=%s\nijk_candidate_version=c92e1e6\n' "$(git rev-parse HEAD)" \
  > android/contrib/build/ijk-candidate-provenance.txt
for abi in arm64 armv7a x86_64; do
  (cd android && revision=c92e1e6 ./compile-ijk.sh "$abi")
done
```

Keep the full IJK source commit, the per-ABI FFmpeg build manifests and the GSY
candidate checksum file together. The override is opt-in and only retains the
historical embedded label; it does not replace the published source identity.
Other build environments still require hash comparison and relevant tests.

## Checks and limits

Native host regression instructions are in [tests/ffmpeg5](../tests/ffmpeg5/README.md)
and [patch validation](../patches/ffmpeg5/README.md). MediaCodec packet/BSF/input
host tests are run with
`ijkmedia/ijkplayer/android/pipeline/tests/run_ffamc_ffmpeg_tests.sh SOURCE HOST_BUILD`.
Host tests and successful compilation do not establish Android playback.

Recorded tests apply to the nine-library candidate identified in the GSY
checksum file, not automatically to every future rebuild:

- Three ABI compilation/linking and static checks passed; 64-bit ELF alignment
  is 16 KB, ARMv7 is 4 KB. Runtime evidence is primarily cloud x86_64 Android.
- Official Android 15/API 35 16 KB emulation: library loading/registrations,
  21/21 core cases plus 52 callback/state assertions, 9/9 extended codec cases,
  and 8/8 scoped RTSP/RTMP/MJPEG cases passed. Actual GSY rotated frames passed
  6/8: all software angles and MediaCodec 90/180 degrees; MediaCodec 0/270
  remained black. A fresh official image also crashed `system_server` before
  app installation. Remaining 16 KB checks were not closed.
- Android 11/API 30 4 KB emulation: HTTPS trusted/wrong CA 2/2, forced RTSP UDP,
  synchronous/asynchronous MediaCodec 2/2, and actual GSY rotated frames 8/8
  passed. Sustained playback reached 1202.4 seconds; create/release passed 50/50.
  Small memory growth was recorded; this is not a zero-leak or unlimited-runtime
  claim. Separate fault-injection test variants passed sync/async software
  fallback 2/2; they are not the production nine libraries.
- 4 KB results do not replace 16 KB sustained-run, HTTPS/forced-UDP,
  sync/async/fault-fallback, or the two unresolved hardware-rotation checks.
  Emulator MediaCodec is not a physical vendor decoder. Real ARM64 devices,
  phones/TVs, multi-camera/NVR endurance and real audiovisual sync remain
  unverified. Explicit player recreation after RTSP/RTMP interruption is not an
  automatic-reconnect guarantee; terminal callbacks were not guaranteed within
  the 16 KB observation window. Fallback may wait for the next keyframe.
- OpenSSL 1.1.1w is retained and outdated. TLS verification remains disabled by
  default; enabling it does not provide complete hostname validation in this
  backend. CA-based HTTPS tests do not establish a complete TLS security policy.
- iOS, legacy armeabi/x86/slim distributions and the minimal profile's missing
  embedded `mov_text` callbacks are outside this migration's completed scope.
  GSY APK/AAR builds succeeded with matching nine-library hashes; this does not
  mean every demo page or permission flow was exercised.

The companion delivery's `ACCEPTANCE.zh-CN.md` and `TEST_SCOPE.zh-CN.md`, with
its hash-indexed raw records, remain the authoritative detailed evidence.

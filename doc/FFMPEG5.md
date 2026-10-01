# FFmpeg 5 Android migration candidate

The `ffmpeg-5.0` branch names the migration series; its actual FFmpeg baseline is
**5.1.10**. This is an additive Android build path. The old `init-android.sh`
continues to select the FFmpeg 4.3 fork; use the new initializer explicitly.

## Release-remediation status (2026-10-01)

Current source adds persistent HEVC parameter-set updates, TLS peer-identity
verification, and OpenSSL **3.5.9**. The HEVC and TLS host regressions passed.
A new three-ABI set of nine native libraries has been built, and the independent
static artifact audit passed **73/73** checks. Binary publication, final GSY
integration and full release acceptance remain separate, unfinished steps.

The initializer now pins the published four-patch FFmpeg source commit
[`5d01026835e0fc18be967461df180432e85db0c7`](https://github.com/CarGuo/FFmpeg/commit/5d01026835e0fc18be967461df180432e85db0c7).
All four patch reverse-checks pass against that exact commit. A complete fresh
initializer run with this new pin has not yet been rerun; do not infer it from
source or shell-syntax checks.

### Current rebuilt-library Android evidence

The completed runs below use the new libraries on a cloud x86_64 Android
11/API 30 emulator with **4 KB** pages:

- Core playback/lifecycle: **21/21** cases passed; TLS: **7/7**; network
  regression semantics: **4/4**.
- H.264 MediaCodec synchronous and asynchronous runs passed. Ordinary HEVC
  MediaCodec synchronous and asynchronous runs passed with the separate HEVC
  MediaCodec option enabled. These are emulator decoder-path checks, not
  physical-device or vendor-decoder certification.
- RTSP TCP, RTSP HEVC TCP, RTMP, MJPEG AVI-over-HTTP, raw MJPEG-over-HTTP,
  live multipart MJPEG, clean finite multipart MJPEG and abrupt multipart
  characterization: **8/8** passed in both ordinary and strict-interruption
  runs. The strict RTSP TCP and RTMP cases observed terminal callbacks after
  approximately 3.9 and 5.1 seconds, then tested explicit new-player reprepare.
  This does not demonstrate automatic reconnect. The abrupt multipart case
  permits either an error or clean completion, while rejecting hangs/crashes.

Remaining limits are material:

- The three-frame HEVC parameter-update fixture passed the synchronous
  MediaCodec gate; its first rendering callback arrived after completion.
  The asynchronous run did not report initial rendering and **failed** that
  gate. Neither result certifies correct Android rendered pixels after a
  parameter update; real decoder pixel correctness is established only by the
  separate host regression evidence described below.
- The extended-codec run used forced MediaCodec across all cases instead of
  the matrix's software-default contract. Six of nine cases failed its forced-
  MediaCodec fallback assertion while using software decoders. This is not
  evidence that those formats are unsupported. The correctly configured
  software-default matrix rerun is pending; no complete new matrix pass is
  claimed from the forced-mode run.
- New **16 KB** testing passed loading/registration inventory only. The core
  run was blocked by a Surface prerequisite timeout before playback, so this
  is not a 16 KB core or sustained-playback pass.
- There is no completed new endurance/soak, physical ARM hardware, or final
  production AAR/APK acceptance claim here. The original nine-library hashes
  and the historical Android records below describe the earlier OpenSSL
  1.1.1w candidate; they do not transfer to this rebuilt set.

## Source identity and companion branches

- IJK starting point: `c92e1e6496c5a479ec93b7b22bf7aa36f1faf31e`.
- [Patched FFmpeg branch](https://github.com/CarGuo/FFmpeg/tree/ffmpeg-5.0):
  current four-patch pin `5d01026835e0fc18be967461df180432e85db0c7`, based on upstream
  `n5.1.10` (`19feb712f5c1821d8a3fa1ad63c5bd2e3b9672eb`).
  Historical two-patch pin: `4a62cc600696982f189fbad6c6ea1593e909b814`.
- [GSY companion branch and packaged-library notes](https://github.com/CarGuo/GSYVideoPlayer/blob/ffmpeg-5.0/doc/ffmpeg-5.0.md).
- Current OpenSSL: `openssl-3.5.9`, commit
  `45e844fa2a14ec92d146bd8f5778ac130b6625fb`, initialized in
  `extra/openssl-3.5.9`. The old `extra/openssl5` directory is preserved.
- Historical frozen candidate: OpenSSL `OpenSSL_1_1_1w`, commit
  `e04bd3433fd84e1861bf258ea37928d9845e6a86`.
- Repaired `libavcodec/hevc_mp4toannexb_bsf.c` SHA-256:
  `168ef23bfeb4f0651a3c7d2713f280c01436f1137004ff5ea507e67a59e7acb4`.
  This is a file hash, not a replacement FFmpeg commit ID.

For a new directory, the initializer clones the named FFmpeg fork branch, fetches
the pinned commit if the branch has advanced, and checks out that exact commit
with a detached HEAD. It verifies the commit and reverse-checks all four
required production patches without applying them. The exact repaired source
commit is published; the fresh initializer integration check remains separate.
Existing unexpected revisions are rejected without fetching, checking out or
resetting them; missing/modified patches also
cause a failure without applying patches. If upgrading an older locally patched upstream checkout,
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
export OPENSSL_SOURCE="$PWD/extra/openssl-3.5.9"
(
  set -e
  for abi in arm64 armv7a x86_64; do
    android/contrib/compile-ffmpeg5.sh "$abi"
    (cd android && ./compile-ijk.sh "$abi")
  done
)
```

This explicitly uses `config/module-lite-more.sh`, including the MJPEG parser,
MPJPEG demuxer, IJK demuxers, HTTPS and H.264/HEVC Annex-B bitstream filters.
API levels are 21 for ARM64/x86_64 and 16 for ARMv7. The build outputs share the
legacy `android/contrib/build` destinations; do not mix previously built
FFmpeg 4 and FFmpeg 5 artifacts. Keep each ABI's `libijkffmpeg.so`,
`libijkplayer.so` and `libijksdl.so` together. No native binaries are committed
to this IJK branch.

OpenSSL static archives now use content-addressed caches under
`android/contrib/build/openssl-<abi>/ffmpeg5-3.5.9/<cache-key>/output`.
The cache identity includes actual source and toolchain bytes, ABI/API,
configuration and host build tools. Reuse also verifies the installed headers
and archive hashes. Old `openssl-<abi>/obj5` and `output` caches are never used.
Changing an input selects a different cache; a partial or modified cache is
rebuilt rather than silently reused. Each TLS output has a `build-manifest.txt`.

For dependency-only build/cache verification, use the same entry point:

```sh
OPENSSL_ONLY=1 android/contrib/compile-ffmpeg5.sh arm64
```

This mode requires `ANDROID_NDK` and `OPENSSL_SOURCE`, uses the same TLS cache
as a full build, and does not require or read `FFMPEG5_SOURCE`. It does not build
FFmpeg or establish player/Android runtime acceptance.

`compile-ffmpeg5.sh` exports `revision=n5.1.10` so FFmpeg's generated version
string stays at the tested value after the patches become a fork commit.
`android/contrib/build/ffmpeg-<abi>/output/build-manifest.txt` records the source
commit, dirty state, version string, patches, profile, configuration, toolchain
and output hash. Pinning source and version alone does **not** guarantee a
byte-identical rebuild; compiler, dependencies, paths and build environment also
matter. Check rebuilt hashes and rerun the relevant runtime tests.

## TLS behavior and dependency notices

With the repaired OpenSSL backend, `tls_verify=1` checks both the certificate
chain and the expected DNS name or IP address. `verifyhost` explicitly overrides
that reference identity, independently of the TCP destination. IP references
require an IP SAN; DNS SANs take precedence over CN. Configuration failures and
invalid explicit CA files fail closed in verified mode.

The default remains **`tls_verify=0`**, and explicit `tls_verify=0` retains the
same unauthenticated compatibility mode. Applications needing authenticated
HTTPS must opt in to verification and supply an explicit, trusted `ca_file`
readable by the Android process. This native build does not automatically use
the Android/Java trust store; the build-time OpenSSL install path is not an
Android runtime CA bundle. Host default-store tests are not Android trust-store
integration evidence.

OpenSSL 3.5's unchanged default security level rejects TLS 1.0/1.1 and weak
keys/signatures. Verification-disabled playback can still fail negotiation
against such legacy endpoints. Compatibility requirements for those endpoints
are pending an explicit decision; this migration does not silently lower the
security level or promise OpenSSL 1.1.1w's handshake compatibility.

The inspected FFmpeg configuration remains `CONFIG_GPL=0`, `CONFIG_VERSION3=0`
and `CONFIG_NONFREE=0`; no GPL/version-3/nonfree switch was enabled for this
remediation. OpenSSL 3.5.9 carries Apache-2.0 terms. The unmodified official
[OpenSSL LICENSE.txt](third-party/openssl-3.5.9-LICENSE.txt) is included for source
and dependency distribution. Preserve the applicable FFmpeg, IJK and OpenSSL
notices with redistributed outputs; these configuration facts are not a new
licensing certification.

## Optional reproduction of the tested candidate

This section is historical. The current repaired filter, TLS backend and
OpenSSL 3.5.9 dependency change the native bytes. The current build entry point
cannot reproduce the old candidate merely by selecting its version label;
reproduction requires the frozen pre-remediation sources, OpenSSL 1.1.1w and
the original build environment.

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

For that explicit historical candidate-reproduction use case, after building
the frozen FFmpeg/OpenSSL inputs, scope the override to each IJK invocation and
record the actual source commit separately:

```sh
mkdir -p android/contrib/build
printf 'ijk_source=%s\nijk_candidate_version=c92e1e6\n' "$(git rev-parse HEAD)" \
  > android/contrib/build/ijk-candidate-provenance.txt
(
  set -e
  for abi in arm64 armv7a x86_64; do
    (cd android && revision=c92e1e6 ./compile-ijk.sh "$abi")
  done
)
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

Current remediation host evidence:

- The full MediaCodec packet/BSF/input runner and ASan+UBSan variant passed with
  the repaired HEVC filter. Real decoder pixel checks cover in-band and
  `AV_PKT_DATA_NEW_EXTRADATA` updates, later IRAPs, transactional rollback and
  flush reset. A real three-frame `hev1` MP4 is covered. Original-source and
  injection-order-only controls expose the old failure. See the
  [HEVC test scope](../ijkmedia/ijkplayer/android/pipeline/tests/README.md).
- TLS identity tests passed 35/35 against OpenSSL 1.1.1w, 3.5.7 and 3.5.9;
  the 3.5.9 ASan+UBSan variant passed 35/35. The real IPv4 resolver-fallback
  build passed 33/33 applicable TLS cases and six resolver checks. Negative
  controls failed as intended. See the companion
  [TLS test instructions](https://github.com/CarGuo/FFmpeg/blob/ffmpeg-5.0/tests/ijk/TLS_IDENTITY.md).
- Sanitizers cover the changed production code and harnesses, not every linked
  archive. LeakSanitizer was unavailable in the ptrace-based environment;
  `detect_leaks=0` is not a leak-check pass. The new Android build and bounded
  runtime results are listed above; remaining Android gates are not closed.

Recorded historical tests apply to the old nine-library candidate identified by
[the immutable pre-remediation GSY checksum file](https://github.com/CarGuo/GSYVideoPlayer/blob/ece13ca87fd1682f4aa3ba97230bf832f23190b6/doc/ffmpeg-5.0.SHA256SUMS),
not to the current rebuilt set or any later replacement of the branch's manifest:

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
- The historical candidate contains outdated OpenSSL 1.1.1w and the previous
  chain-only verification backend. Its CA-based HTTPS passes do not establish
  hostname validation. The current source repairs identity verification and
  selects OpenSSL 3.5.9; its separate new Android evidence and remaining
  failures are described above.
- iOS, legacy armeabi/x86/slim distributions and the minimal profile's missing
  embedded `mov_text` callbacks are outside this migration's completed scope.
  GSY APK/AAR builds succeeded with matching nine-library hashes; this does not
  mean every demo page or permission flow was exercised.

The companion delivery's `ACCEPTANCE.zh-CN.md` and `TEST_SCOPE.zh-CN.md`, with
its hash-indexed raw records, remain the authoritative detailed evidence.

### Follow-up runtime status (2026-10-01 13:56 UTC)

The attempted 1,200-second live-HLS soak did not finish: the emulator disappeared and the runner exited unsuccessfully at09:24 UTC. The retained report is empty, so server requests or elapsed wall time are not a sustained-playback pass. Both simultaneously running emulator processes reported a hanging QEMU main loop; the root cause has not been established. The new API36 16KB environment also has no completed playback result. A single-emulator retry is in progress. The separately rebuilt GSY verification APK packages all nine new libraries byte-identically, uncompressed at16KiB ZIP offsets; packaging verification is not a16KB runtime pass.

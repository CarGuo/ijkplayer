# FFmpeg 5.1.10 ijk integration patches

## Locked source revisions

- Upstream repository: https://github.com/FFmpeg/FFmpeg
- Upstream release: `n5.1.10`
- Upstream commit: `19feb712f5c1821d8a3fa1ad63c5bd2e3b9672eb`
- FFmpeg 5 fork branch: https://github.com/CarGuo/FFmpeg/tree/ffmpeg-5.0
- Pre-remediation FFmpeg 5 fork pin: `4a62cc600696982f189fbad6c6ea1593e909b814`
- Published four-patch fork commit (previous initializer pin): `5d01026835e0fc18be967461df180432e85db0c7`
- Published recovery parent: `eeda3f84b84b95cbecc5604b343a9100ad656311`
- Previous published five-patch initializer pin: `ff87490262107e004f6a16765484e36e1973d705`
- Recovered five-patch compiled-code checkpoint: `0cb0a06ef3d2b8e2814cb0e60be99d7986e1bde1` (same production code as the previous five-patch pin; updated publication docs)
- Previous six-patch RTSP initializer pin: `c157591220febd96fdcffb05283f63dd38627081`
- Exact seven-patch TLS/HLS initializer pin: `0c8735b51d29dbc74e18c235246d5230bd5db989`
- Existing fork repository: https://github.com/CarGuo/FFmpeg
- Existing fork tag: `ijk-n4.3-20260301-007`
- Existing fork commit: `9e7c61dfc2b8f370a4851b9001bd6e37ce673903`
- Existing fork base: `n4.3` (`8e12af29d1a3f95c9e952d78354e3c8b1c0431a8`)

The Android setup is `./init-android-ffmpeg5.sh`; it checks out the exact
pinned source and reverse-checks all seven required production patches. It never
reapplies them or resets an existing checkout. A different existing revision,
a modified/missing required patch, or a non-repository target directory fails
without overwriting it. In particular, the previous `extra/ffmpeg5` checkout
is deliberately preserved; use a separate IJK workspace for fresh recovery.

The current source adds `0007-hls-tls-options.patch` to the six-patch RTSP pin.
It preserves explicit TLS and proxy policy through nested HTTP(S)-backed HLS
resource opens and keepalive fallback; it does not enable verification by default.
The exact current pin is `0c8735b51d29dbc74e18c235246d5230bd5db989`. Its production tree is
`1b66ddd0115c0c4e558224cef421f0ebeb67e264`, identical to the locally reviewed
`c9fc8ab40e548ea4546edd9e870da5f57b4d78f0`; publication changed commit metadata
and message, not source bytes. The production-only seventh patch SHA-256 is
`15a775141617e7d9377c34c62b764c629ffd7537e78ef035f820ed6c08c02cb5`.

The complete network-disabled initializer integration passed ten cases at the
public pin, including standalone-bundle recovery and fifth/sixth/seventh patch
removal/preservation. A fresh full remote initialization remains unrun. Host
HTTP+HTTPS, HTTPS-only and ASan/UBSan policy regressions passed. The current
ARMv7 tuple also has bounded physical-device TLS/HLS and 1x/3x playback evidence;
the current ARM64 and x86_64 tuples remain build/static-only. See the
[current Android scope](../../doc/FFMPEG5.md#current-tls-fixes-and-qualification-boundary)
and [FFmpeg test scope](https://github.com/CarGuo/FFmpeg/blob/0c8735b51d29dbc74e18c235246d5230bd5db989/doc/tests/hls-tls-options.md).

Historical five-patch publication: the previous immutable pin above included
all five patches and documentation; its production FFmpeg code matched the
recovered five-patch compiled checkpoint. Exact remote commit/tree/blob checks
passed for that earlier publication. Those historical checks are not a claim
of new RTSP-pin initialization, compilation or runtime verification.

To initialize from the delivered local source repository (or a standalone Git
bundle containing the exact recovery commit), use an explicit override:

```sh
FFMPEG5_SOURCE_REPOSITORY=/absolute/path/to/FFmpeg ./init-android-ffmpeg5.sh
# Alternatively:
FFMPEG5_SOURCE_REPOSITORY=/absolute/path/to/FFmpeg.bundle ./init-android-ffmpeg5.sh
```

The override accepts an existing local path, not a replacement network URL.
Relative paths are resolved from the IJK repository root. The exact commit and
all seven patch checks remain mandatory. The override only selects FFmpeg;
OpenSSL, libyuv, and SoundTouch still use their published pins and require
network access unless their pinned checkouts are already present. See
[initializer tests](../../tests/ffmpeg5/README-initializer.md) for the bounded,
network-disabled local recovery verification and preservation checks. See the
[build and validation notes](../../doc/FFMPEG5.md) for Android build evidence
and remaining acceptance limits.

The patches remain available to reconstruct the same source changes from
pristine upstream FFmpeg 5.1.10. The first two patches also contain their host
regressions; HEVC tests are in the companion IJK tree and TLS tests are in the
FFmpeg fork's `tests/ijk` directory. This alternative production-source
reconstruction does not depend on a pre-modified checkout. Apply all seven
patches, in order:

```sh
git clone --depth 1 --branch n5.1.10 https://github.com/FFmpeg/FFmpeg.git ffmpeg5
cd ffmpeg5
test "$(git rev-parse HEAD)" = 19feb712f5c1821d8a3fa1ad63c5bd2e3b9672eb
git apply --check /path/to/ijkplayer/patches/ffmpeg5/0001-ijk-hooks.patch
git apply /path/to/ijkplayer/patches/ffmpeg5/0001-ijk-hooks.patch
git apply --check /path/to/ijkplayer/patches/ffmpeg5/0002-mpjpeg-eof.patch
git apply /path/to/ijkplayer/patches/ffmpeg5/0002-mpjpeg-eof.patch
git apply --check /path/to/ijkplayer/patches/ffmpeg5/0003-hevc-parameter-updates.patch
git apply /path/to/ijkplayer/patches/ffmpeg5/0003-hevc-parameter-updates.patch
git apply --check /path/to/ijkplayer/patches/ffmpeg5/0004-tls-peer-identity.patch
git apply /path/to/ijkplayer/patches/ffmpeg5/0004-tls-peer-identity.patch
git apply --check /path/to/ijkplayer/patches/ffmpeg5/0005-mov-hevc-seek-replay.patch
git apply /path/to/ijkplayer/patches/ffmpeg5/0005-mov-hevc-seek-replay.patch
git apply --check /path/to/ijkplayer/patches/ffmpeg5/0006-rtsp-initial-open.patch
git apply /path/to/ijkplayer/patches/ffmpeg5/0006-rtsp-initial-open.patch
git apply --check /path/to/ijkplayer/patches/ffmpeg5/0007-hls-tls-options.patch
git apply /path/to/ijkplayer/patches/ffmpeg5/0007-hls-tls-options.patch
```

## Preservation inventory

The old fork is 13 commits above n4.3, affecting 10 files. This port preserves:

1. Mutable placeholders and size-checked registration for six custom protocols:
   `ijkmediadatasource`, `ijkhttphook`, `ijklongurl`, `ijksegment`, `ijktcphook`,
   `ijkio`
2. Both custom demuxer placeholders and registration: `ijklivehook`, `ijklas`
3. The newer old-fork fix making async registration a no-op, avoiding a write
   into the built-in const `ff_async_protocol`
4. Concat `cur_file_no` propagation, while retaining FFmpeg 5's additional
   per-file option dictionary and error handling
5. `AVApplicationContext` and all existing event/control payload layouts,
   event IDs, callback dispatch, HTTP filesize and async statistics APIs
6. Hexadecimal pointer option helpers in `libavutil/dict`, with defined
   `uintptr_t` formatting, bounds checking and malformed-input rejection
7. Installed private headers and `avc.o` required by the ijk implementation

Additional FFmpeg 5 compatibility details:

- `application.h` explicitly includes `stddef.h` and `stdint.h`;
  `application.c` explicitly includes `error.h` and `mem.h` rather than relying
  on removed transitive includes
- Installed `libavformat/internal.h` requires the matching
  `libavcodec/packet_internal.h`, which is included in the installed header set
- Protocol and demuxer placeholders are declared mutable consistently; no
  upstream const object is overwritten
- Null registration objects are rejected in addition to ABI-size mismatches
- The old fork provides application event APIs but does not patch `http.c` or
  `tcp.c` to call them. This patch preserves the actual old-fork behavior; it
  does not claim new HTTP/TCP instrumentation coverage

## Original hook validation

- Pristine n5.1.10 archive accepts the patch, and all resulting changed files
  match the development checkout byte-for-byte
- Native GCC build with PNG, H.264, AAC, PCM, MOV, WAV, concat, custom protocols
  and custom demuxers passed
- `tests/ijk/run-contract.sh` passed all documented contracts
- ASan and UBSan with the changed source instrumented passed; upstream static
  archives were not instrumented
- LeakSanitizer is blocked by the execution environment's ptrace implementation;
  it is not reported as a pass
- Installed-header compilation passed for application, AVC, ID3v2 and private
  AVIO headers using the generated FFmpeg config header

Run instructions and the detailed contract coverage are installed by this patch
under `tests/ijk/README.md`. Device playback, JNI, Android MediaCodec and GSY UI
behavior require the separate ijkplayer validation suite.

## 0002: exact MPJPEG final-delimiter EOF

`0002-mpjpeg-eof.patch` is a separate patch against upstream n5.1.10 and can be
applied after `0001`. It fixes standards-valid multipart closing delimiters
returning INVALIDDATA: preserve the actual first delimiter and recognize only
its exact terminal form, retaining permissive ordinary-part behavior, malformed
header rejection and EIO propagation. It also preserves stable EOF past MIME
epilogues. No broad `endswith("--")` heuristic is used.

Actual-source host regression: pristine upstream **9/22**, patched **22/22**,
patched **ASan+UBSan 22/22**. Thirteen red cases exercise terminal MIME delimiters and related edge cases. Strict/permissive modes, byte-seek/EOF/replay, max-length70 boundary tokens and
padding, hyphen-ending boundary tokens, malformed headers, short/corrupt
payloads and injected EIO are covered. Pristine patch
application and byte-for-byte reconstruction passed. Details and reproduction
commands are in `tests/ijk/README-mpjpeg-eof.md`, included by this patch.

This is demuxer/AVIO regression evidence, not a JPEG decoder or Android playback
pass. It does not enable the separately required `mjpeg` parser or `mpjpeg`
demuxer in a build profile. Android rebuild and final runtime tests are separate.

## 0003: persistent HEVC parameter updates

`0003-hevc-parameter-updates.patch` repairs `hevc_mp4toannexb` caching. Current
VPS/SPS/PPS are keyed by type, layer and ID; in-band updates and recognized hvcC
`AV_PKT_DATA_NEW_EXTRADATA` persist into later IRAPs. NAL-length changes and
outgoing Annex-B side data are handled without mutating the demuxer's initial
CSD. A malformed packet rolls back its staged cache changes. Flush restores the
initial CSD and NAL length; seeking into a changed configuration requires the
demuxer to resend the applicable update. Already-Annex-B input remains a
passthrough. Two-pass hvcC sizing retains SEI order without per-SEI cache nodes.

The repaired source file's SHA-256 is
`168ef23bfeb4f0651a3c7d2713f280c01436f1137004ff5ea507e67a59e7acb4`.
The full normal and ASan+UBSan host runners passed, including pixel checks on a
real three-frame `hev1` MP4, persistent in-band/side-data updates, malformed
rollback, length changes, repeated flush, multiple IDs/layers and 4096 SEI NALs.
Original-source and injection-order-only negative controls expose the failure.
See the [IJK HEVC host suite](../../ijkmedia/ijkplayer/android/pipeline/tests/README.md).
This does not certify Android MediaCodec reconfiguration or physical decoders.

## 0004: TLS peer identity and resolver fallback

`0004-tls-peer-identity.patch` makes `tls_verify=1` verify the chain and the
expected DNS name or IP address. An explicit `verifyhost` is the reference
identity; IP references require IP SANs and DNS SANs take precedence over CN.
Invalid CA/configuration input fails closed. Server/listen mode retains client
chain verification without imposing the server URL's name on the client.
The real IPv4-only resolver fallback returns `EAI_NONAME` for nonnumeric input
with `AI_NUMERICHOST`; allocation failures remain errors rather than being
mistaken for DNS identities.

Default and explicit `tls_verify=0` still do not authenticate the server.
Android applications need an explicit trusted CA file when enabling verification;
the native backend does not automatically use Android's Java trust store.
OpenSSL 3.5.9 is selected by the IJK build, not installed by this FFmpeg patch.
Its default security policy can reject old TLS versions and weak keys even with
verification disabled; legacy-endpoint compatibility is not yet accepted.

The [TLS host suite](https://github.com/CarGuo/FFmpeg/blob/ffmpeg-5.0/tests/ijk/TLS_IDENTITY.md)
passed 35/35 on OpenSSL 1.1.1w, 3.5.7 and 3.5.9, with 35/35 under ASan+UBSan on
3.5.9. The real fallback build passed 33/33 applicable cases plus six resolver
checks. Unmodified-backend and unmodified-fallback negative controls failed as
expected. The OpenSSL archive was not sanitizer-instrumented, and LeakSanitizer
was not available. These results establish host behavior only; the new Android
build and runtime tests remain pending.

## 0005: MOV HEVC sample-description replay after seek

`0005-mov-hevc-seek-replay.patch` replays the selected HEVC sample description
through packet NEW_EXTRADATA after a seek, including reselection of non-target
tracks in interleaved seek mode. The last-description state changes only after
side-data allocation succeeds, permitting a failed allocation to be retried.
Fragment-only, exhausted, and other excluded tracks are guarded.

This patch contains the production `libavformat/mov.c` change. The pinned
recovery commit also carries its newly reconstructed fixture generator, runner,
guard checks and documentation in `tests/mov-hevc-seek/`. Applying production
patches alone does not recreate those companion test files or the commit ID.
The recovery tests cover 72 exact-YUV seek comparisons and 12 helper checks;
these are bounded host results, not restored historical reports or Android
vendor MediaCodec certification. Refer to the delivered source's
`tests/mov-hevc-seek/README.md` for their scope and prerequisites.

## 0006: bounded RTSP initial-open redirects and network errors

`0006-rtsp-initial-open.patch` is the frozen three-file production patch for
`libavformat/rtsp.c`, `rtsp.h` and `rtspdec.c`. SHA-256:
`cdbda16f783f5a2dfbe228c4f5a7ee8dacce643cc667a9f0ecc5cab23cf74e14`.
It extends initial-open redirect handling to SETUP and initial PLAY, clears
replaced session/SDP state, bounds redirect chains and preserves network errors.
The paired IJK changes preserve RTSP timeout and terminal-error handling and
add the opt-in live compressed-queue policy. FFmpeg and IJK source must be used
as the matching RTSP candidate; the patch alone does not add the IJK policy.

The host network tests live in the companion FFmpeg repository; the IJK
control-flow regression is `tests/ffmpeg5/test_rtsp_network_policy.py`, and the
live-policy reproduction instructions are in
[README-rtsp-live.md](../../tests/ffmpeg5/README-rtsp-live.md). Production patches
alone do not recreate every companion fixture/test file or a published commit ID.

Content-Base/SDP control URLs on another port still use the existing single
control connection; independent multi-control sessions are unsupported.
Playback-time REDIRECT, later PLAY/seek redirects and automatic reconnect are
outside this patch. See [RTSP network limits](../../doc/RTSP-NETWORK.md).

Current Android evidence is limited to 12 passed executed RTSP cases and one
MediaCodec N/A on the API 29 x86_64 software emulator. Separate 3x qualification
fails for both candidate and previous-branch baseline. This RTSP candidate has
no physical-device qualification; optional HTTPS/HLS smoke was not run and no
end-to-end latency SLA is claimed. See the
[current evidence and historical R3 separation](../../doc/FFMPEG5.md).

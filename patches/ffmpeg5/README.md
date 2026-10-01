# FFmpeg 5.1.10 ijk integration patches

## Locked source revisions

- Upstream repository: https://github.com/FFmpeg/FFmpeg
- Upstream release: `n5.1.10`
- Upstream commit: `19feb712f5c1821d8a3fa1ad63c5bd2e3b9672eb`
- FFmpeg 5 fork branch: https://github.com/CarGuo/FFmpeg/tree/ffmpeg-5.0
- Pre-remediation FFmpeg 5 fork pin: `4a62cc600696982f189fbad6c6ea1593e909b814`
- Repaired four-patch fork commit (current initializer pin): `5d01026835e0fc18be967461df180432e85db0c7`
- Existing fork repository: https://github.com/CarGuo/FFmpeg
- Existing fork tag: `ijk-n4.3-20260301-007`
- Existing fork commit: `9e7c61dfc2b8f370a4851b9001bd6e37ce673903`
- Existing fork base: `n4.3` (`8e12af29d1a3f95c9e952d78354e3c8b1c0431a8`)

The normal Android setup is `./init-android-ffmpeg5.sh`; it checks out the
pinned `CarGuo/FFmpeg` fork commit and reverse-checks all four required
production patches. It never reapplies them. The current pin contains all four
patches and has passed their reverse checks. The historical pre-remediation pin
contains only the first two and is not the current build input. A fresh full
initializer run with the new pin has not yet been rerun. See the
[build and validation notes](../../doc/FFMPEG5.md) for the completed three-ABI
build, bounded Android results and remaining acceptance failures.

The patches remain available to reconstruct the same source changes from
pristine upstream FFmpeg 5.1.10. The first two patches also contain their host
regressions; HEVC tests are in the companion IJK tree and TLS tests are in the
FFmpeg fork's `tests/ijk` directory. This alternative production-source
reconstruction does not depend on a pre-modified checkout. Apply all four
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

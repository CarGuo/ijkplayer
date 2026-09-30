# FFmpeg 5.1.10 ijk integration patch

## Locked source revisions

- Upstream repository: https://github.com/FFmpeg/FFmpeg
- Upstream release: `n5.1.10`
- Upstream commit: `19feb712f5c1821d8a3fa1ad63c5bd2e3b9672eb`
- FFmpeg 5 fork branch: https://github.com/CarGuo/FFmpeg/tree/ffmpeg-5.0
- FFmpeg 5 fork commit: `4a62cc600696982f189fbad6c6ea1593e909b814`
- Existing fork repository: https://github.com/CarGuo/FFmpeg
- Existing fork tag: `ijk-n4.3-20260301-007`
- Existing fork commit: `9e7c61dfc2b8f370a4851b9001bd6e37ce673903`
- Existing fork base: `n4.3` (`8e12af29d1a3f95c9e952d78354e3c8b1c0431a8`)

The normal Android setup is `./init-android-ffmpeg5.sh`; it checks out the
pinned `CarGuo/FFmpeg` fork commit and verifies both patches are already present
with reverse-apply checks. It never reapplies them. See [build and validation
notes](../../doc/FFMPEG5.md).

The patches remain available to reconstruct the same source changes from
pristine upstream FFmpeg 5.1.10, including native host regression tests. This
alternative reconstruction does not depend on a pre-modified checkout. Apply
both patches, in order:

```sh
git clone --depth 1 --branch n5.1.10 https://github.com/FFmpeg/FFmpeg.git ffmpeg5
cd ffmpeg5
test "$(git rev-parse HEAD)" = 19feb712f5c1821d8a3fa1ad63c5bd2e3b9672eb
git apply --check /path/to/ijkplayer/patches/ffmpeg5/0001-ijk-hooks.patch
git apply /path/to/ijkplayer/patches/ffmpeg5/0001-ijk-hooks.patch
git apply --check /path/to/ijkplayer/patches/ffmpeg5/0002-mpjpeg-eof.patch
git apply /path/to/ijkplayer/patches/ffmpeg5/0002-mpjpeg-eof.patch
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

## Validation

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

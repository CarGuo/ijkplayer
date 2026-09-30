# FFmpeg 5 core regression tests

Run against a **native host build** of the matching patched FFmpeg sources:

```sh
tests/ffmpeg5/run-host.sh ../ffmpeg5 ../build/ffmpeg-host
SANITIZE=address,undefined,float-cast-overflow tests/ffmpeg5/run-host.sh ../ffmpeg5 ../build/ffmpeg-host ../build/ijk-core-tests-asan
```

The native FFmpeg build must enable the PNG encoder/decoder, H.264 and PCM S16LE
decoders, MOV/WAV demuxers, file protocol, swscale, and zlib. The fixture generator
uses an installed `ffmpeg` with lavfi and libx264 (`FFMPEG=/path/to/ffmpeg`).
`CC` selects the host C compiler. No Android device or network is used.
The default optimization flags mirror Android's `-O3 -ffast-math` followed by the
player module's `-fno-fast-math` override. NaN is an intentional clock sentinel,
so `-ffast-math` must not be the final setting for this module. Running with
`CLOCK_CFLAGS='-O3 -ffast-math'` is an expected-failure control that reproduces
the former invalid-clock bug (including with the Android NDK's Clang used as a
host compiler).

The tests include the production C translation units directly. This keeps static
queue, decoder, snapshot, and LAS helpers under test without maintaining a second
implementation. Unused sections are discarded at link time. Timer/profiling
plumbing in `test_core.c` is stubbed; SDL locks/conditions and FFmpeg codec and
format implementations are real.

Coverage:

- RTSP/MMSH read-pause policy: buffering-only pause continues input, explicit
  user pause suspends reads, and single-step permits reads (all eight combinations)

- Strict API compilation of core, metadata, and non-JNI custom format sources
  with implicit declarations, incompatible pointers and discarded const rejected
- Playback clock NaN/serial fallback, infinity and double-to-int64 overflow,
  missing seek timestamp, valid timestamp progression/backward seek, start-time
  adjustment, raw negative PTS, and NaN-aware clock synchronization at release
  optimization
- PCM decode packet retention after injected EAGAIN, timestamp rescaling,
  missing-PTS continuation, stale serial buffer release, EOF drain, seek reset,
  and abort
- H.264 B-frame presentation order and all 12 delayed frames received before EOF
- Consecutive PNG snapshots with pixel verification, aspect-ratio sizing,
  64-bit filenames, terminal/nonterminal callbacks and failed file output
- LAS public custom AVIO allocation, two opens, stream preservation on reopen,
  explicit ownership, failed-input cleanup and repeated close
- IJK dictionary pointer roundtrip including high-bit values, null/missing keys,
  invalid strings and overflow; uses IJK's own dictionary API

Sanitizer mode instruments the IJK translation units. To instrument FFmpeg too,
configure and rebuild its native libraries with matching sanitizer flags. These
host tests do not validate Android hardware codecs, device audio/video rendering,
real network reconnects, DRM, or application-level GSYVideoPlayer behavior.

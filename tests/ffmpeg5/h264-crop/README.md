# Opt-in H264 crop host regression tests

Run from any directory against the matching **native host** FFmpeg 5.1.10
headers and static libraries. Android libraries cannot be linked into these host
executables. The project's pinned FFmpeg source is
`CarGuo/FFmpeg@0c8735b51d29dbc74e18c235246d5230bd5db989` (the seven-patch source
prepared by `init-android-ffmpeg5.sh`). No additional FFmpeg patch is introduced
by the crop workaround.

```sh
FFMPEG_SOURCE_DIR=/path/to/patched/ffmpeg5 \
FFMPEG_BUILD_DIR=/path/to/native-host-build \
OUTPUT_DIR=/tmp/ijk-h264-crop-tests \
    bash tests/ffmpeg5/h264-crop/run-host.sh
```

The equivalent positional form is:

```sh
bash tests/ffmpeg5/h264-crop/run-host.sh \
    /path/to/patched/ffmpeg5 /path/to/native-host-build /tmp/ijk-h264-crop-tests
```

Requirements: Bash, Python 3, a C11 compiler (`CC`, default `cc`), zlib, pthreads,
and a little-endian host. GCC or Clang must support AddressSanitizer and UBSan
for the default run. Both normal and sanitized modes run by default. To run only
one explicitly, set `MODES=normal` or `MODES=sanitized`. No network, fixture
generator, Android SDK, emulator, or physical device is used by the runner.
If omitted, the output directory is a new temporary directory, printed on
completion. Generated headers, executables, logs, and input receipts are placed
there, not in tracked source files. Any compile or test failure stops the run.

## Preparing a native FFmpeg build

With the matching patched checkout already present, the following is the minimal
host configuration used for the recorded regression run. Use fresh directories;
do not overwrite an Android build or reapply the seven source patches.

```sh
export FFMPEG_SOURCE_DIR=/absolute/path/to/patched/ffmpeg5
export FFMPEG_BUILD_DIR=/absolute/path/to/new-native-host-build
mkdir -p "$FFMPEG_BUILD_DIR"
(
    cd "$FFMPEG_BUILD_DIR"
    "$FFMPEG_SOURCE_DIR/configure" \
        --disable-everything --disable-programs --disable-doc --disable-x86asm \
        --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample \
        --enable-decoder=h264,hevc --enable-parser=h264,hevc \
        --enable-demuxer=mov --enable-protocol=file \
        --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb
    make -j2
)
bash tests/ffmpeg5/h264-crop/run-host.sh
```

The runner links `libavcodec/libavcodec.a` and `libavutil/libavutil.a`, reads the
source and build headers, and uses `-lm -pthread -lz`. A larger FFmpeg build may
need additional external-library linker flags and has not been certified by this
runner. This small configuration is **not** sufficient for the separate general
`tests/ffmpeg5/run-host.sh` suite, which needs other codecs and components.

## What is exercised

Production headers are included directly. `extract_helpers.py` extracts current
feeder, EOF-prefix, checked JNI lifecycle, and eligibility functions from current
production files for compilation with test scaffolding. It fails if expected
source boundaries disappear, rather than silently testing an old copied helper.
The generated helper bytes matched the frozen reviewed helper bytes in the
initial portability run.

- Original suite: 36 PASS observations per mode across generated full/small A/B parameter
  changes, Annex B and 1/2/4-byte avcC framing, source-AU retention, fresh BSF
  caching/flush, generation cancellation, borrowed-input checks, feeder behavior,
  and injected stop/release/CSD failures.
- Baseline epoch: 16 observations per mode, including real-marker restoration,
  explicit headerless-B downgrade, full-B reentry, null Surface, EOF and a later
  marker, configuration failure, and OFF/ON comparisons of 288 ordinary H264
  B-frame packets with two marker flushes. Bytes, PTS/DTS/duration and no additional
  codec configuration are checked for those ordinary packets.
- Coexistence: 10 observations per mode, covering nine real-FFmpeg
  NEW_EXTRADATA/baseline-ownership scenarios and the actual 16-combination
  eligibility gate. Resolution-change handling remains enabled.
- Independent core: four observations per mode, including 40 alternating crop
  transitions, preserved avcC High-profile extensions, unsupported source shapes,
  and 100,000 deterministic SPS/PPS mutation pairs.

These overlap: 66 PASS lines per mode are not 66 unrelated end-to-end cases.
Malformed NEW_EXTRADATA controls intentionally emit FFmpeg decoder errors in
`coexistence.stderr`; nonempty stderr alone is not a failure classification.
An assertion, sanitizer failure or nonzero exit is a failed run.

## Limits and source integrity

FFmpeg BSF/parser/codec-parameter routines run for real. Codec, SDL and JNI
operations are deterministic fakes. These tests do not certify actual Android
decoder behavior, Surface recovery, all-frame delivery, A/V synchronization,
performance, arbitrary seek targets, GSY application integration, or the wider
FFmpeg regression suite.

The default sanitizer flags instrument the test translation units and included
production helpers. The retained FFmpeg static libraries used in the recorded
run were not fully sanitizer-instrumented. Full-library sanitizer coverage needs
a separately instrumented FFmpeg build. Leak detection is disabled by default;
no leak-checking claim is made. `ASAN_OPTIONS` and `UBSAN_OPTIONS` may be explicitly
overridden for a local investigation.

`verify_inputs.py` checks every generated bitstream against `fixtures/manifest.json`
and records repository-source, FFmpeg-header, configuration and archive hashes.
The runner compares before/after receipts to detect changes during execution.
Receipts have repository-relative names and no embedded workstation paths.
See [fixture generation and provenance](fixtures/README.md) for the complete
local generation command and tool versions. The runner tests the current
production bytes, not fixture-specific
production branches or a separate reimplementation of the crop logic.

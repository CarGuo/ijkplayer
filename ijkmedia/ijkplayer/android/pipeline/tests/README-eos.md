# Recreated MediaCodec EOS/drain and startup fallback fixes

This patch was reimplemented on published ijkplayer commit
`5f83a204553c5f0eaa4cda4cf5b6b900a81d7164`. It is **not** a recovered copy of
the missing historical pipeline or its Android binaries.

## State contract

- A compressed EOF is a pending null packet without side data, in the current
  queue serial. BSF output must be drained before one codec EOS is submitted.
- Keep that null packet pending after EOS submission. This prevents another
  queue read from blocking the synchronous output loop and prevents replayless
  software fallback. Stop/seek waits are bounded.
- Consume configuration output and empty EOS without rendering. EOS with a
  payload remains a picture. A zero-size non-EOS Surface picture is valid.
- After EOS, drain reorder slots. Publish `Decoder.finished` only on a later
  output iteration, after the last returned picture has been queued/dropped.
  Existing ffplay completion also requires the picture queue to be empty.
- Codec flush/start clears EOS/reorder state. Actual-output evidence is monotonic
  for the node and is preserved across seek/recreation. Audio startup's shared
  `first_frame_decoded` value cannot disable startup fallback.
- Propagate input queue, output release, proxy allocation, codec flush and picture
  enqueue errors. Codec teardown remains responsible for codec-owned buffers.

## Host tests

Standalone production EOS/policy helper tests, using deterministic SDL/codec
fakes (no FFmpeg or Android SDK needed):

```sh
SANITIZE=1 tests/run_ffamc_eos_tests.sh /tmp/eos-test
```

Run that command from the pipeline directory. The full existing suite now also
runs these tests:

```sh
SANITIZE=1 tests/run_ffamc_ffmpeg_tests.sh FFMPEG_SOURCE HOST_BUILD OUTPUT_DIR
```

HOST_BUILD requires H264/HEVC decoders, parsers and Annex B BSFs plus MOV/file.
The input tests compile the production `prepare_input_packet` implementation and
cover filtered/unfiltered EOF, the pending EOF latch, early flush requests,
seek serial reset, side-data-only input and injected codec flush failure in
both feeder modes. Test mode selection does not emulate true Android scheduling.

If LeakSanitizer reports that ptrace prevents it running, `ASAN_OPTIONS=detect_leaks=0`
permits address/undefined-behavior checks, but **does not provide a leak check**.
The suite instruments test code and the changed HEVC BSF; full-library sanitizer
coverage requires an instrumented FFmpeg build.

## Release validation still required

Host fakes and NDK syntax compilation do not qualify an Android release. On the
exact rebuilt APK/so set, verify short-video final frames and completion,
EOS-with-payload and empty EOS, repeated seek-after-EOF, stop while waiting,
synchronous/asynchronous MediaCodec, codec-start failure and EOF-time failure,
Surface zero-size output, real Surface replacement and vendor reorder behavior.
EOF failure must report an error rather than complete blank or fall back with no
input to replay. Preserve original failed/inconclusive historical tests separately.

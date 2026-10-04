> Historical checkpoint: the following record describes 2026-10-02 only.
> Current source pins, completed R3 physical playback scope and remaining limits
> are in [FFMPEG5.md](FFMPEG5.md). Its then-open HLS/rate gates do not override
> the current bounded results.

# Reconstructed FFmpeg 5 candidate — 2026-10-02

This candidate rebuilds missing post-publication changes on published IJK commit
`5f83a204553c5f0eaa4cda4cf5b6b900a81d7164`. It is not the byte-identical lost
working directory and is not yet qualified for release.

## Recreated player behavior

- Both MediaCodec feed modes queue a single EOS marker per serial and preserve
  the compressed EOF latch. Completion waits for output EOS, its possible final
  picture, reorder slots and the player picture queue. Seek/flush resets the
  per-stream state; codec failures remain errors.
- Startup software fallback uses actual MediaCodec picture output rather than a
  shared first-frame flag that audio startup may set after a timeout. Fake frames,
  codec configuration and empty EOS markers do not count as real video output.
- Android audio clocks are anchored after decoded PCM, manual-start waiting and
  backend latency are ready. This prevents slow codec initialization being added
  to media position. Non-Android timing is unchanged; there is no position clamp.
- The FFmpeg companion replays active HEVC sample descriptions after seek/BSF
  flush. See the fifth standalone patch and its companion repository tests.

## Fresh focused verification

- Production input/EOS helpers: H264 and HEVC filtered/unfiltered EOF and seek
  behavior, with focused ASan/UBSan tests and independent state-transition review.
- Output-ready clock: 12 Android and 10 non-Android deterministic scenarios,
  normal and UBSan. Tests extract real production functions. Numeric assertions
  reject NaN/infinity; explicit unknown/invalid serial cases are checked separately.
  The original baseline reproduces 6.650 seconds instead of 0.150 seconds.
- MOV: 72 first-picture exact-YUV seeks, 12 guard/allocation checks and an
  independent 462-frame seek-to-EOF comparison. The original baseline fails.
- All three target pipeline syntax checks and complete native build/link passed.
  The first nine-library static audit passed 85 checks; runtime remains separate.
- LeakSanitizer is unavailable in this environment. Host tests do not establish
  Android Surface ownership, real thread scheduling or vendor codec behavior.

Commands and test scope:

- `tests/ffmpeg5/test_audio_ready_clock.py .`
- `tests/ffmpeg5/test_native_outputs.py`
- `tests/ffmpeg5/check_native_outputs.py . --output /path/to/new-report.json`
- `ijkmedia/ijkplayer/android/pipeline/tests/run_ffamc_eos_tests.sh`
- `ijkmedia/ijkplayer/android/pipeline/tests/run_ffamc_ffmpeg_tests.sh FFMPEG_SOURCE HOST_BUILD`

Refer to each adjacent test README for tool/options. Never overwrite historical
results or carry their pass counts to a new binary.

## Rebuild and provenance

Use the exact source initializer pin and five standalone patches. The FFmpeg
recovery commit must be published before default remote initialization can fetch
it; an explicit local repository override can reproduce a supplied source bundle
without publishing. An old unexpected checkout is preserved and rejected.

The profile remains `config/module-lite-more.sh`. NDK r22b preserves API16 for
ARMv7 and API21 for ARM64/x86_64. Application.mk now states ARMv7 API16 explicitly,
and obsolete GCC4.9 selectors for ARMv7/x86_64 use Clang. Both 16KB linker flags
are retained for 64-bit builds. Keep each ABI's three libraries together and
retain source commits, source modifications, dependency/build manifests and SHA256.

## Remaining release gates

Android SDK/tool installation and fresh exact-binary playback are incomplete.
Required acceptance includes 4KB/16KB load and playback, rotation with actual
rendered pixels, seek/release/lifecycle, interruption and sustained playback,
GSY packaging, and device-specific decoder limitations. ELF alignment alone is
not a 16KB runtime pass. Emulated x86_64 codecs do not certify physical ARM/vendor
hardware.

The later missing HTTPS-HLS child-option propagation and expanded playback-speed
work have not been reconstructed or validated. They remain open release blockers.
No native package from this tree may be labelled fully verified or published as
the stable release until its remaining gates and release approval are resolved.

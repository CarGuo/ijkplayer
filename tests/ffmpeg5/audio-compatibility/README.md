# AudioTrack compatibility regressions

Run from the repository root with the pinned SoundTouch checkout initialized by
`init-android-soundtouch.sh` (Bilibili `ijk-r0.1.2-dev`, commit
`6bf39cd3bf6b0c156267d12446b0d6bdcfcd53c2`):

```sh
python3 tests/ffmpeg5/audio-compatibility/run.py \
  --soundtouch-root ijkmedia/ijksoundtouch --out /tmp/ijk-audio-compat
```

Requires host C/C++ compilers and Python 3. Output, extracted source and logs stay
under `--out`, outside tracked tests. Runs normal, optimized/no-fast-math and
ASan+UBSan builds. Leak detection is disabled in traced runners.

Coverage:

- Actual head tracker plus extracted production clock: mixed 1×/3×/1× queues,
  positive short prefixes, unknown timestamps, discontinuities, pause and seek
- Exact JNI transaction with mocked JNI calls: rejected 2.5×/3×, mutation before
  exception, incorrect speed/pitch/fallback, read-back failure, verified rollback,
  and fatal unverified restoration
- API 16–22 checked sample-rate read-back/rollback and verified 1× handoff
- Exact rate setter: software bounds 0.25×–4×, just-outside values, non-finite,
  zero, negative and extreme positive inputs, before prepare and while active
- Real pinned SoundTouch through the bounded production adapter: mono/stereo
  output matches a correctly drained direct-engine reference, exact rounded
  output-frame totals through EOS, bounded-buffer canaries, 1→3→1 segments,
  fresh-instance seek reset and signed-counter boundary detection
- Actual synthesized sine PCM: nonzero energy and dominant FFT frequency retained
  at 0.25×, 0.5×, 1×, 2×, 3× and 4×, in addition to frame-count checks

This is host regression evidence. JNI is mocked; these tests do not prove an
Android device's playback timing, hardware capabilities or audible quality.

`run.py --only rate --source-revision b3a5d9e597d1b9e445927dda30b435589f0ef181`
extracts the old production JNI/setter code and fails the recoverable-rejection
assertion. Supply the same `--soundtouch-root` and a separate `--out` directory.

Also run the unchanged ownership/pause/EOF gates (whose stubs now include current
head tracking and engine controls):

```sh
python3 tests/ffmpeg5/audio-submission/run_extract.py --out /tmp/ijk-audio-owner
python3 tests/ffmpeg5/audio-submission/close/run_close.py --sanitizers --out /tmp/ijk-audio-close
python3 tests/ffmpeg5/pause-clock/run.py --out /tmp/ijk-audio-pause
python3 tests/ffmpeg5/test_audio_ready_clock.py .
```

The submission suite includes actual backend request/apply/notify sequencing and
live engine-handoff schedules, including concurrent rate changes and seek/option
cancellation in either direction. Rejected controls must preserve exact PCM suffixes;
engine handoff must never write silence or flush accepted same-generation real PCM.
The startup suite below permits a checked reset only for an exclusively synthetic
physical epoch that has never accepted real PCM.
EOF still requires transform exhaustion, no outstanding real submission, matching
generation/epoch, and the final acknowledged output frame actually played.

## Exact producer with real resampling

Use the pinned native FFmpeg source and a matching static host build (as for
`tests/ffmpeg5/run-host.sh`):

```sh
python3 tests/ffmpeg5/audio-compatibility/run_producer.py \
  --ffmpeg-source /path/to/pinned/ffmpeg5 \
  --ffmpeg-build /path/to/ffmpeg-host-build \
  --soundtouch-root ijkmedia/ijksoundtouch --out /tmp/ijk-audio-producer
```

This extracts the actual producer and its helpers, uses real libswresample and
SoundTouch, and mocks only the frame queue, clock dependencies and SDL locks.
It checks 96→48 kHz, 44.1→48 kHz, late source-rate changes, compensation, retained
resampler EOF samples/endpoints, short SoundTouch streams, video/external masters,
finite/unknown/discontinuous PTS, 1→3→1 buffer rates, pause/seek reset, and live
engine-control boundaries without consuming the next engine's input early.
Runs normal, optimized and ASan+UBSan modes. Together with the backend-owner suite,
this distinguishes producer/tail correctness from transaction/device behavior.

For the combined production initialization, producer, callback and owner path,
run `../audio-startup/run.py` with the same dependency flags. Its AudioTrack model
holds the head at zero until 18,448 frames are filled, reproducing the pre-first-
decode synthetic-silence/control deadlock in r3. See `../audio-startup/README.md`.

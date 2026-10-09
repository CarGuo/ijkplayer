# Audio startup liveness regression

Run from the repository root using existing pinned host dependencies:

```sh
python3 tests/ffmpeg5/audio-startup/run.py \
  --ffmpeg-source /path/to/pinned/ffmpeg5 \
  --ffmpeg-build /path/to/ffmpeg-host-build \
  --soundtouch-root /path/to/pinned/soundtouch \
  --out /tmp/ijk-audio-startup
```

The test extracts production `stream_open` mode/rate/clock initialization,
the complete audio producer, callback, submission/EOF policy, AudioTrack owner
and public rate/engine setters. It links real libswresample/libavutil and pinned
SoundTouch. Frame queues, SDL locks and platform calls are deterministic models.
The device head stays zero until 18,448 frames are written. A paused or A/V-not-
ready producer first creates its actual 512-byte synthetic buffer; the owner
accepts two 256-byte callbacks before the initial non-unit-rate control boundary.

With `--source-root /path/to/r3`, the same regression fails with exit 42:
128 synthetic frames accepted, zero played, zero real frames decoded, and no
further writes after 100 owner polls. The timeout injects abort only to end the
test, and is not a production timeout or workaround. The old producer-only
test deliberately initializes a selected rate; this test retains production's
initial software rate of 1.0.

The repair permits a checked pause/flush/new-epoch reset at a control boundary
only for an unplayed, exclusively synthetic physical epoch. A sticky tracker
flag records any successful real-PCM acceptance, including zero-valued samples,
unknown PTS and spans subsequently reclaimed. That flag is cleared only by
tracker initialization or a checked physical-epoch reset. It is not inferred
from sample amplitude, timestamps, media duration or the remaining span queue.

Coverage in normal, `-O3 -fno-fast-math` and ASan+UBSan modes:

- Paused and A/V-gated startup at 0.25, 0.5, 1, 2, 3 and 4×
- Pre-first-real engine switches in both directions and latest-rate controls
- Paused control commit, pending/failed/committed seek, abort and new seek epochs
- Zero-valued unknown-PTS real PCM with synthetic padding, including reclaimed
  real spans; these epochs retain the normal physical drain barrier
- Partial real writes, a paused unacknowledged suffix and the next callback's PCM
- Successful/coalesced real accepts, rejected and empty accepts, and flag reset
- Checked pause/flush failures, epoch overflow, unavailable/stale head reads
- Recoverable platform-rate rejection and fatal platform failure

One `IJK_AUDIO_SYNTHETIC_EPOCH_RESET` line per attempted synthetic reset records
generation, old/new epoch, accepted/played frames, `real=0` and checked result.
There is no per-buffer diagnostic. A real-PCM epoch is never flushed by this
exception; ordinary real engine-drain and EOF rules are unchanged.

Logs, extracted code, compiler commands and source hashes stay in `--out`.
No network, device, installation or actual audio operations occur. These tests
do not qualify Android hardware timing, audibility, actual JNI rollback or
whole-player concurrency. Existing producer, owner, pause, close and JNI/tempo
suites must still be run; Android startup repetitions remain a separate gate.
Leak detection is disabled in traced runners; address/undefined checks are on.

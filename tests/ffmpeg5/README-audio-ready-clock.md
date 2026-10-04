# Android audio-ready clock regression

Run from the repository root:

```
python tests/ffmpeg5/test_audio_ready_clock.py .
SANITIZE=undefined python tests/ffmpeg5/test_audio_ready_clock.py .
```

This extracts exact production callback/clock functions into a deterministic host
harness with timer, decode and backend stubs. It covers slow codec startup,
latency-query delay, manual start and abort, pause, retained PCM across repeated
callbacks, unknown/negative PTS, and stale/invalid serials including a seek during
the manual-start wait. It checks non-Android callback-entry timing is preserved.
It does not simulate MediaCodec or certify Android/device playback.

With HEAD at the original 5f83a204553c5f0eaa4cda4cf5b6b900a81d7164 baseline,
`python tests/ffmpeg5/test_audio_ready_clock.py . --baseline` is an expected-failure
control: the old callback publishes 6.650s instead of 0.150s after a simulated
6.5-second startup delay. A later HEAD is not the original negative control.

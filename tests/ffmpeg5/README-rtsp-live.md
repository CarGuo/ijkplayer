# Opt-in RTSP live queue recovery

This candidate adds a player option, `rtsp-live-max-buffer-ms`, for applications
that know the selected RTSP stream is live. Default `0` preserves existing
behavior. Supported nonzero values are `500..60000` ms; start with `1200` or
`3000`, and allow at least two packet intervals for unusually low-frame-rate
sources. A shorter value can cause more visible jumps and audio discontinuities.

```java
ijkMediaPlayer.setOption(IjkMediaPlayer.OPT_CATEGORY_PLAYER,
        "rtsp-live-max-buffer-ms", 1200);
```

GSY's IJK backend passes the same player-category option through
`VideoOptionModel`. Apply it before preparation to the known live playback, not
globally to recordings. This is an internal native-source change and requires
rebuilding the matching native libraries; changing Java options alone does not
add the implementation to an older binary.

## Behavior and invariants

- Only opt-in RTSP with unknown/nonpositive duration at normal 1x speed uses the
  policy. Seek, explicit pause and stepping retain the old read policy. Known
  RTSP VOD and other protocols are untouched.
- Live reads keep draining the transport instead of moving backlog into socket
  or server queues. Packet timestamp span or the existing byte budget triggers
  a reset of all selected stream generations.
- Existing queue serial checks invalidate old decoder frames, buffered PCM and
  playback-head submissions. Video restarts on a key packet; preceding audio is
  discarded to that key packet's PTS. A pending keyframe gate remains effective
  if the application changes rate while recovery is in progress.
- A backward DTS epoch resets the queue as well. PTS-only backward reordering
  is not treated as an epoch reset. If timestamps are unavailable, only the byte
  fallback can bound storage; there is no justified time-age guarantee.
- First-frame and subsequent rebuffer high-water marks are clamped below the
  queue age cap. Caps below 500 ms are rejected during preparation.
- The byte budget permits one individually oversized audio/key packet plus
  flush markers, so a legitimate large keyframe is not discarded forever.
  It is not a strict allocation limit on the demuxer's incoming packet.

This is a compressed-packet-queue policy, **not a glass-to-glass latency bound**.
Camera encoding, server history, TCP backlog, decoder/output queues and display
latency remain outside the measured local span. Waiting for a keyframe may take
longer than the cap, and an absent keyframe can leave output waiting while input
continues to be discarded with bounded queues. Expired RTSP sessions after long
pause still need an application-level reopen; this option does not reconnect.

## Reproduction and measurements

The fixture sends valid loopback RTSP PCMU + H.264 over TCP or UDP. Its RTP origin
is tied to the server monotonic clock. The C harness includes the production
`ff_ffplay.c`, uses its real packet queues/filter and exact FFmpeg5 demux/decoders,
and paces a deterministic software consumer. It does not instantiate Android
AudioTrack, MediaCodec, Surface or the complete player read/render thread.

Recorded controlled cases:

| Case | Default final audio age | 1200 ms policy final audio age | Decode errors |
|---|---:|---:|---:|
| 24 s TCP, no stall | 0.801 s | not needed | 0 |
| 24 s TCP, four 0.8 s output stalls | 4.001 s | 0.842 s | 0 |
| 24 s UDP, four 0.8 s output stalls | 4.001 s | 0.821 s | 0 |
| 20 s TCP, 3 s read + output pause | 3.801 s | 0.041 s | 0 |
| 120 s TCP, twenty-four 0.8 s output stalls | 20.002 s | 0.821 s | 0 |
| 1200 s TCP, 250 output stalls, then ~200 s recovery observation | not rerun | 0.101 s | 0 |

The 120 s queue peak was 1,046,365 bytes by default versus 44,812 bytes with the
policy. A PLAY redirect from an SDP advertising 120 s of recording into the live
fixture also passed with the paired FFmpeg redirect fix: duration became unknown,
the live policy activated, generations advanced, and both codecs decoded cleanly.

The startup latency in these controls comes largely from probing and the modeled
consumer. It is not an estimate of camera or Android device latency. The tests
reproduce accumulation from repeated consumer stalls; they do not prove every
camera's long-play symptom has this cause. The 20-minute candidate soak completed
with 46,722 decoded audio frames and 23,398 video frames, zero decode errors,
52,386 bytes consumer-sampled peak queue storage, and sampled RSS of
5448..6760 KiB. In the last 190 s after stalls ended, audio age stayed between
100.73 and 101.79 ms; its measured slope was 0.000335 ms/s. The cap is still a
local compressed-queue policy, not a promise of 1200 ms camera-to-screen age.

All 1200 integer-second bins are present: 948 have valid A/V age and 252 explicitly
record unavailable ages during decoder/keyframe restarts. The maximum gap between
valid age reports is 3.002 s. Recovery intentionally skips stale media and can
produce a visible jump or short audio gap; this is not seamless playback proof.
RSS is a sample range, not a leak check or a process-lifetime high-water claim.

The frozen-source comparison rebuild and the soak executable have byte-identical
retained live-policy, queue, clock, consumer and network functions. Their whole
executable text differs by one assertion source-line immediate after a later,
unexercised read-thread error diagnostic change. The provenance receipt records
both binary identities and that exact delta, rather than claiming an identical
whole-file build or full player read-thread execution.

## Running focused tests

```sh
bash tests/ffmpeg5/run-rtsp-live.sh FFMPEG_SOURCE FFMPEG_HOST_BUILD OUTPUT
ASAN_OPTIONS=detect_leaks=0 SANITIZE=address,undefined,float-cast-overflow \
  bash tests/ffmpeg5/run-rtsp-live.sh FFMPEG_SOURCE FFMPEG_HOST_BUILD OUTPUT_ASAN
```

The host FFmpeg build needs RTSP/SDP, TCP/UDP/RTP, H.264/PCMU decoders and the
existing run-host.sh codecs/demuxers for the full core regression suite. The
fixture generator uses the system FFmpeg/libx264 only to create a tiny deterministic
H.264 source. The target decoder and network behavior use the explicitly linked
FFmpeg5 libraries, not the system FFmpeg.

Unit tests exercise default/VOD/protocol/pause/step/seek/rate guards, bounded
missing-keyframe wait, audio-only/video-only, backward DTS reset, PTS reordering,
the single-packet byte exception, real first-frame buffering readiness, old
AudioTrack submission COMMIT/CANCEL rejection, newer submission ownership and
old/current rate acknowledgments after a live generation reset.

ASan/UBSan pass for the IJK test translation unit. LeakSanitizer is unavailable
under this traced runtime and is explicitly disabled; FFmpeg static libraries
are not sanitizer-instrumented. Existing core/clock/decode/LAS/dictionary host
regressions pass. Historical audio-submission/close extraction harnesses no
longer compile against the current R3 playback-head/rate definitions and are
reported as blocked, not passing. Android build, hardware codec flushing,
playback-head concurrency, audible continuity and device soak remain separate
verification gates.

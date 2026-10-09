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

## Historical reproduction and measurements

The recorded measurements below belong to earlier fixture/source snapshots.
They are not a network or device acceptance result for this LOCAL-only follow-up.

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

The harnesses in this directory provide focused host coverage. Sanitizer
instrumentation covers the IJK test translation units; linked FFmpeg libraries
require a separately instrumented build for equivalent coverage. LeakSanitizer
is disabled in traced runs. The audio-submission and close harnesses have been
updated for the current interfaces; run them separately against the same source
snapshot. No current Android runtime, hardware codec, audible-output or
device-soak qualification is implied.

## `ijklivehook` and option-boundary follow-up

The accepted cap remains `0` (off) or `500..60000` ms. Both integer and string
setters store the option until preparation. Invalid extremes, malformed strings
and `1..499` now consistently return `AVERROR(EINVAL)` before audio output/read
threads open. On Android this is the synchronous `IjkMediaException` from
`prepareAsync()`, not a later `OnError` callback. Other player options keep their
existing permissive dictionary-error behavior; this validation is specific to
the live cap.

An enabled cap also supports `ijklivehook:rtsp://...`, using a wrapper-owned
atomic capability derived from the **actually opened inner demuxer and its
duration**. URL prefixes and the outer wrapper name do not establish live RTSP
identity. After a hook rewrite or reconnect, capability is recomputed; known recordings,
non-RTSP media, failed opens and interrupts disable it. Player threads never borrow a
replaceable inner context pointer. As for other AVFormatContext operations, the
outer context must stay alive until all reader threads have joined.

The wrapper exposes FORMAT `timeout` (RTSP microseconds) and `rtsp_transport`
options, preserving explicit values and forwarding them to post-hook RTSP URLs
on each open. Omitted values preserve FFmpeg defaults; these RTSP-specific values
are not forwarded when the hook changes the URL to another protocol. This is a
bounded forwarding fix, not a claim that arbitrary format options pass through
this FFmpeg5 wrapper.

Only when the player cap is enabled, the player supplies the internal
`ijklivehook-stable-streams=1` mode. Outer stream objects and time bases then stay
fixed across reconnects; new packet timestamps/durations are rescaled into those
original time bases. Reconnect discontinuities invalidate the old live queues,
advance selected stream serials together, and restart video on a key packet.
Audio alignment from the previous timestamp epoch is cleared. A pending decoder
restart still finishes if the new source is VOD or a different protocol, but no
new age-cap drops are initiated for that source.

Changing stream count/order or codec configuration cannot safely reuse the
already-open player decoders. In this opted-in mode it produces a latched
`AVERROR(ENOSYS)` with a diagnostic requiring an application-level player reopen,
rather than repeatedly reconnecting or appending streams under active decoders.
Stream identity is positional, with codec/layout/extradata compatibility checks;
identical same-codec tracks reordered by a server are not distinguishable here.
The default cap-off wrapper retains its legacy reconnect/stream behavior.
Ordinary transport failures still use the wrapper's existing reconnect policy,
with its application callback and interrupt callback controlling retries. The
wrapper does not extend this cap to untrusted networks or enable it by default.

Run the wrapper-specific checks in addition to the existing queue harness:

Run deterministic in-process checks without sockets first:

```sh
IJKLIVEHOOK_LOCAL_ONLY=1 bash tests/ffmpeg5/run-ijklivehook.sh FFMPEG_SOURCE FFMPEG_HOST_BUILD OUTPUT
```

Omitting `IJKLIVEHOOK_LOCAL_ONLY` enables additional loopback network tests;
those are separate from LOCAL-only acceptance:

```sh
bash tests/ffmpeg5/run-ijklivehook.sh FFMPEG_SOURCE FFMPEG_HOST_BUILD OUTPUT
ASAN_OPTIONS=detect_leaks=0 SANITIZE=address,undefined,float-cast-overflow \
  bash tests/ffmpeg5/run-ijklivehook.sh FFMPEG_SOURCE FFMPEG_HOST_BUILD OUTPUT_ASAN
```

`test_ijklivehook.c` uses the production wrapper callbacks, FFmpeg's actual outer
option/open dispatch, and scripted inner I/O for rewritten URL/duration/protocol,
repeated reconnect, time-base rescaling, topology rejection, and bounded
interrupt/retry cases. `test_ijklivehook_network.c` uses the production wrapper and
actual RTSP demuxer over loopback; it checks effective socket timeout, negotiated
transport, outgoing TCP/UDP SETUP, a read stall followed by reconnect, and a
positive-duration RTSP recording guard. The queue harness now registers/links the
production wrapper and accepts a wrapped URL, and tests the real cap parser and
both public native setters in the preparation-time helper. It still does not
instantiate the full Android player/read/render threads or verify hardware
codecs, audible output, Android exceptions, or a device soak.


The follow-up compatibility guard distinguishes decoder configuration from
advisory metadata for the checked H264/HEVC/AAC/AAC-LATM, PCMU/PCMA/PCM-S16LE and
G726/G726LE decoder contracts. Bitrate estimates and ordinary container tags
may change, and unknown sample-format/profile/level metadata may become known,
without forcing a reopen. H264's special SMV2 tag remains significant. G726's
coded sample width is initialization data: G726-32 to G726-24 cannot reuse the
old decoder. Equivalent legacy/new channel-layout representations are accepted.
For other codecs, no advisory exemption has been established; remaining
initialization fields (including bitrate/tag/block sizes) must match, so those
formats can still require application reopen on metadata differences. This is
a bounded codec-specific compatibility policy, not a universal decoder proof.
Callback cancellation and read interruption are latched for the wrapper session;
subsequent reads cannot revive the old inner source after cancellation.

For deterministic in-process tests only, set `IJKLIVEHOOK_LOCAL_ONLY=1` when
running `run-ijklivehook.sh`. This mode runs no socket/loopback test. It includes
G726 codeword-width rejection and repeated reads following callback cancellation
or a cleared transient interrupt.


A failed seek after a latched terminal RTSP/wrapper error now leaves buffering
off. The terminal read loop also releases buffering entered while the remaining
tail drains, before backpressure/completion checks. These calls use the existing
pause updater: explicit user pause and single-step/paused-seek preview requests
remain intact. A successful seek still clears the terminal latch, flushes the
selected queue generations and resumes ordinary buffering. This prevents an
unfillable buffering pause from hiding the terminal error indefinitely.

Run the local production-block regression with real buffering/pause helpers:

```sh
python3 tests/ffmpeg5/test_terminal_seek.py FFMPEG_SOURCE FFMPEG_HOST_BUILD OUTPUT
```

It extracts the actual seek and terminal-loop blocks, substitutes only the seek
result, and starts no network I/O or player threads. Cases cover failed terminal
seek, undrained/drained tails, one error/no rereads, user pause/step, successful
seek generations, nonterminal rebuffering and autoexit. `--repo PREVIOUS_CHECKOUT`
runs identical assertions against another tree as a negative control.


### Terminal error state and repeated failed seeks

A failed seek after the terminal error was already reported keeps the existing
terminal-notification latch. The failed seek still reports its own result through
`FFP_MSG_SEEK_COMPLETE`; it does not emit the original read error a second time.
A successful in-flight seek clears the read-error latch as before. Normal EOF,
auto-looping, explicit pause/step, tail draining and nonterminal seek buffering
keep their existing behavior.

Delivery of `FFP_MSG_ERROR` now enters the documented public `MP_STATE_ERROR`
before invoking the listener. `isPlaying()` is false in that state, and existing
start/pause/seek guards reject those commands. Applications must reset/reopen
after `OnError`. Android JNI still discards the native seek return value, so a
post-error `seekTo()` can return without throwing, without issuing a native seek
or producing `OnSeekComplete`. Delayed prepared/completed messages and duplicate
errors cannot replace the error state; errors for inactive/stopped states are
ignored. Android's existing completion fallback when an `OnErrorListener` returns
false is unchanged.

The public-state regression executes production `ijkmp_get_msg`, command guards,
message queues and seek/pause helpers. It includes preparing/started/paused errors,
queued controls, a stop between dequeue and dispatch, aborted old-player queues,
a separate replacement player, and ordinary EOF-to-seek/start. It does not run
JNI, Android reset/release, a device, or a full read/render thread:

```sh
python3 tests/ffmpeg5/test_player_error_state.py FFMPEG_SOURCE FFMPEG_HOST_BUILD OUTPUT
ASAN_OPTIONS=detect_leaks=0 SANITIZE=address,undefined,float-cast-overflow \
  python3 tests/ffmpeg5/test_player_error_state.py FFMPEG_SOURCE FFMPEG_HOST_BUILD OUTPUT_ASAN
```

Both Python regressions accept `--repo PREVIOUS_CHECKOUT` for an identical negative
control. The terminal-block regression also covers repeated failed seeks after a
reported `AVERROR_EXIT`/`ENOSYS`, normal EOF and ordinary auto-loop behavior.


The ERROR transition uses the existing error event without adding a separate
`FFP_MSG_PLAYBACK_STATE_CHANGED`. The iOS bridge already emits its playback-state
notification and then its error-finish notification from ERROR; the native
cardinality regression requires exactly one ERROR, zero extra STATE_CHANGED
messages and unchanged following-event order. Normal completion/start transitions
still emit their existing state-change events. No platform bridge code changes.

Queue abortion still cannot retract an error already dequeued before shutdown.
The stop-dispatch and replacement-queue tests cover their specific orderings;
they do not establish comprehensive Android release/reset race elimination.

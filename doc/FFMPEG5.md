# FFmpeg 5 Android integration: TLS/HLS candidate

## Current source and initializer

`ffmpeg-5.0` is the migration branch name; the upstream baseline is **5.1.10**.
The current seven-patch initializer pins FFmpeg
[`0c8735b51d29dbc74e18c235246d5230bd5db989`](https://github.com/CarGuo/FFmpeg/commit/0c8735b51d29dbc74e18c235246d5230bd5db989).
This adds explicit nested-HLS TLS policy inheritance to the preceding RTSP pin
`c157591220febd96fdcffb05283f63dd38627081`. The older five- and six-patch pins
remain historical identities. Do not reset an older checkout in place; use a
separate workspace.

The initializer reverse-checks all seven production patches without reapplying
or resetting an existing checkout. Build receipts include all seven patch
hashes. OpenSSL 3.5.9, libyuv and SoundTouch dependency pins and the explicit local
repository/bundle override remain unchanged. The complete offline initializer
integration passed ten cases at the current public pin, including fresh local
repository and standalone-bundle initialization; fifth/sixth/seventh missing
patch preservation; wrong-revision, non-repository and invalid-override handling;
and unchanged original checkouts. This does not certify a fresh full network
initialization. See [initializer scope](../tests/ffmpeg5/README-initializer.md)
and [patch provenance](../patches/ffmpeg5/README.md).

## Current TLS fixes and qualification boundary

OpenSSL Configure adds `-fno-unroll-loops` only for `armv7a` when the selected
compiler defines `__clang_major__ == 11`. The flag is included in the existing
content-addressed TLS cache key. OpenSSL 3.5.9 source, certificate/peer-name
verification, NDK r22b, and the ARMv7 API 16 floor are unchanged. ARM64 and x86_64
receive no new OpenSSL flag.

The FFmpeg change preserves explicitly supplied TLS policy through HTTP(S)-backed
HLS child playlists, reloads, keys, segments and initialization opens, including
keepalive fallback. It preserves explicit empty proxy overrides and fails closed
if policy copying fails. Verification is not enabled by default. Explicit peer
names and client-authentication files follow nested resources, as for existing
HTTP redirects; do not supply client-authentication settings to an untrusted
playlist. Custom/local initial AVIO remains the I/O provider's responsibility.
See the [source regression and exact option scope](https://github.com/CarGuo/FFmpeg/blob/0c8735b51d29dbc74e18c235246d5230bd5db989/doc/tests/hls-tls-options.md).

The earlier ARMv7-only workaround passed a bounded Pixel 3 / API 28 direct-HTTPS
comparison. It did not exercise nested HLS policy and does not qualify the new
combined candidate. See the [original comparison and reusable optimizer tests](../tests/ffmpeg5/tls-sparse-array/README.md).
The combined candidate rebuilds all nine matched FFmpeg/player/SDL libraries for
ARMv7, ARM64 and x86_64 against the current FFmpeg pin. The current ARMv7 tuple
was exercised in a 32-bit Java/JNI process on a physical Pixel 5 / API 30 in the
2026-10-10 TLS/HLS run.
The suite reports **15 passed, 1 failed** across
16 cases; the sole failure reproduces the original ARMv7 CA-loading crash. The
overall matrix remains failed, not all green. Candidate CA loading, default and
verified direct HTTPS, wrong-CA/DNS/IP rejection, and nested-segment wrong-CA/SAN
rejection passed. Trusted HLS fetched the master, media playlist and all 13
segments at 1x and 3x; the 24.021-second fixture completed at 3x in 8.754 seconds
with a sampled media-position slope of 3.0028x and all 35 interior rate readings
equal to 3. The paired 1x/3x timing and slope gates passed. Three official OpenSSL
ARMv7 source-test executables passed separately against static libcrypto; they
are not tests dynamically linked to the IJK library.

The preceding 14-pass/2-fail attempt remains recorded under its original
pre-start getter oracle. The rerun corrected when the harness observes applied
speed; it changed neither the native tuple nor its clock implementation.

The separate 2026-10-10 ARM64 run
exercised the final ARM64 tuple in a verified
64-bit Java/JNI process on a physical Pixel 5 / API 30. It reports **12 passed,
1 failed** across 13 cases; the sole failure is again the original ARMv7
CA-loading negative control, executed in an isolated 32-bit child. That matrix
also remains failed. Final ARM64 CA loading, default/trusted direct HTTPS,
wrong-CA/DNS/IP rejection, HLS 1x/3x playback and nested-segment wrong-CA/SAN
rejection passed. Both HLS runs fetched master/media playlists and all 13
segments. The 24.021-second media completed at 3x in 8.585 seconds with a
sampled position slope of 2.9853x; all 35 interior speed readings were 3, and
all 109 interior 1x readings were 1. The paired timing/slope gates passed.
Official OpenSSL unit executables were not run on ARM64; the ARMv7 source-unit
evidence above remains separate.

Current x86_64 libraries have build/static verification only, with no runtime
result for those bytes. These ARMv7 and ARM64 results do not qualify TLS 1.3,
all HLS key/init/live/redirect paths, video or hardware decoding, acoustic
quality, external networks, long sessions, all devices or 16 KiB-page runtime.
Static checks and historical runs must not be reported as a current whole-release
pass.

## Current audio source behavior and limits

These audio compatibility changes are later than the historical emulator,
Pixel 8 and compiled identities recorded below; those older artifacts do not
qualify the current source. The current three-ABI build uses `module-lite-more.sh`.
The earlier, pre-TLS/HLS ARM64 tuple passed the 2026-10-09 Pixel 5 / API 30
core gate: 14 checks passed, none failed and three were explicitly skipped.
That result is historical and does not qualify the newly rebuilt ARM64 tuple.
The current rebuilt ARM64 tuple has separate, bounded TLS/HLS and 1x/3x audio
evidence above; that run does not repeat this broader historical audio gate.
Current x86_64 remains build/static-only. ARM64/x86_64 16 KiB ELF alignment is
a static layout check, not 16 KiB-page device-runtime coverage.
That historical ARM64 run's exercised
scope includes software tempo 0.25x–4x, A/V playback at 2x, 96 kHz resampling,
and three captured-audio content checks. The two very short audio-tail checks
and the device-dependent rejected-rate restoration check remain unqualified.
This is not universal device, hardware
codec, perceptual audio-quality, full A/V-sync or 16 KiB-page runtime qualification.
HE-AAC/SBR-specific late signaling and explicit non-audio master selection were
not exercised through the public Java device harness.

- `soundtouch=1` selects bounded software tempo on AudioTrack, including API 23+.
  The supported software range is 0.25×–4×, including 3×. Invalid or out-of-range
  requests leave the active/requested rate unchanged; they are not clamped.
- `soundtouch=0` selects device-dependent checked platform control: PlaybackParams
  on API 23+ (retaining the existing pitch), or native playback sample rate on
  API 16–22 (which also changes pitch). Rejection is recoverable only after
  read-back verifies restoration of the prior full tuple: speed, pitch and
  fallback mode on API 23+, or the prior sample rate on API 16–22. Unverified
  restoration remains an output error. On the active AudioTrack rate-controlled path, the getter reports the
  applied/playing media rate; before preparation or outside that path it can
  report the requested rate.
- The live integer `soundtouch` option supports GSY's rate-then-option call order.
  The string option remains prepare-time configuration. Live handoff drains old
  transformed PCM and waits for the physical playback head through acknowledged
  output before switching engines. Software requires verified hardware 1×/pitch-1
  to prevent double tempo. A paused queued handoff waits for resume while seek
  and abort remain serviced. Temporary hardware controls do not replace the
  latest user rate; a cancelled handoff retains the committed engine policy.
- A control boundary may retire an underfilled startup epoch only after proving
  that it has accepted synthetic silence and no real PCM, using checked
  pause/flush/reset. This prevents waiting forever for a playback head which
  cannot start below the device's minimum fill. Epochs that accepted any real
  PCM retain the physical drain barrier, including real audio containing zeros.
- Resampling, compensation and SoundTouch use per-output-span estimated media
  PTS, separate from acknowledged physical-frame drain accounting. Queued spans
  keep their own rates and PTS gaps. Resampler/software tails, retained partial
  writes, generation/epoch and the final played frame still gate EOF. A 96 kHz
  input can be resampled to a negotiated 48 kHz output; this is not native
  high-sample-rate AudioTrack output support.

See [audio compatibility regressions](../tests/ffmpeg5/audio-compatibility/README.md)
and [submission/EOF regressions](../tests/ffmpeg5/audio-submission/README.md) for
runnable host tests and their mocked-JNI/head limitations.

## Current RTSP behavior and limits

- Initial-open RTSP redirects include SETUP and initial PLAY, bounded redirect
  chains, session cleanup and origin-specific authentication handling. This does
  not add playback-time REDIRECT, later PLAY/seek redirects or automatic reconnect.
- RTSP FORMAT `timeout` is in microseconds and is preserved through the IJK hook.
  Fatal RTSP read errors retain the original error while the existing tails drain.
- The opt-in PLAYER `rtsp-live-max-buffer-ms` option defaults to `0`. Nonzero
  `500..60000` ms limits apply to the selected live RTSP compressed-packet queues
  at normal 1x speed, with documented timestamp, keyframe and byte-budget limits.
  Recovery intentionally discards old media and can produce jumps or audio gaps.
- A different port in Content-Base or an SDP control URL still does not create an
  independent control connection. General multi-control-session support remains
  unsupported; do not treat Content-Base as a 302 Location. Gateway semantics
  and the known limitation are explained in [RTSP network notes](RTSP-NETWORK.md).

Set PLAYER `rtsp-live-max-buffer-ms` before preparation to `0` (off) or
`500..60000` ms. Invalid integer or string values fail preparation. The cap
applies only at normal 1x playback to actually opened live RTSP media with
unknown/nonpositive duration; it is a compressed-queue policy, not an
end-to-end latency bound.

For `ijklivehook`, live capability is recomputed from the opened inner demuxer
and duration after hook rewrites/reconnects. Explicit FORMAT `timeout`
(microseconds) and `rtsp_transport` are forwarded to post-hook RTSP URLs;
arbitrary format-option forwarding is not promised. With the cap enabled,
reconnects preserve outer stream objects/time bases and rescale packets.
Incompatible stream topology/codec configuration returns terminal `ENOSYS`
and requires an application-level player reopen. Callback cancellation and
read interruption remain terminal for that wrapper session. Ordinary transport
failures retain the wrapper's existing callback-controlled retry policy.

Asynchronous native errors enter the documented Error state before invoking the
listener. The state guard drops duplicate errors and errors processed while
stopped or inactive. It does not retract an event already dequeued before
shutdown; applications must retain their existing session/lifecycle guards.
After Error, seek/start/pause remain rejected under the existing API contract; reset or recreate the player before preparing again. A failed seek
already in flight does not re-emit an unchanged terminal RTSP error, and late
prepared/completed messages cannot replace Error. Normal completion remains
seekable. Java's existing seek return/exception behavior is unchanged.

The [live-policy host tests](../tests/ffmpeg5/README-rtsp-live.md) include a bounded
20-minute software-consumer soak. Neither local queue age nor the emulator
measurements below establish an end-to-end/camera-to-screen latency SLA.

## Historical emulator evidence: 2026-10-05

The earlier frozen RTSP artifacts passed **12 executed cases** in their final
prospective gate. One MediaCodec case was explicitly **N/A**, not passed. Scope: API 29, x86_64 software emulator,
4096-byte guest pages and generated TCP/H.264/AAC fixtures. The frozen interval
observer was independently replayed over 118 captures; each of four scored
windows had 10/10 captures. Default-control latency-growth lower bound was
3102.058 ms; cap=1200 growth upper bound was 518.639 ms, satisfying the existing
bounded differential test. This is a scoped RTSP result, not an all-tests pass.

The separate corrected pre-EOF rate/seek run passed 0.25x, 0.5x, 1x and 2x and
the corrected visible 2-second seek case. **3x failed**: candidate measured
2.3375191424..2.3382352941x, below the unchanged 2.4x floor. The previous-branch
baseline measured 2.1647982063x and also failed its completion bound. A baseline
failure does not turn the candidate result into a pass or prove the cause.
The rate/seek run and final RTSP run used different instrumentation APKs with
the same host/native candidate. Earlier failed and superseded-oracle runs remain
historical failures or inconclusive results under their original oracles.

That earlier RTSP candidate was **not tested on a physical device**.
Optional external HTTPS/HLS smoke was not run in this emulator gate. There is no
new physical MediaCodec, ARM runtime, 16 KiB-page runtime, real-camera/TLS/UDP/H.265,
audible-quality or full A/V-sync qualification. This branch is **not a stable
release or universal 3x playback qualification**.

## Historical RTSP native build identity

The earlier tested RTSP artifacts used the local compiled snapshot label
`v0.8.8-carguo-35-gd5c0b4eb-platformrate-r3-rtspfix-72edc055`.
That label is **not a newly published Git commit**. Frozen source hashes and native
build receipts bind the tested source and artifacts. Publishing the source or
changing the initializer pin does not rebuild the previously tested libraries
or establish bit-identical fresh builds. Keep each ABI's FFmpeg/player/SDL tuple
matched; never mix the candidate libraries with the previous baseline tuple.

## Historical platformrate-r3 record: published before RTSP

The complete preceding published R3 document is retained below with its original
claims scoped to that older candidate. In particular, its Pixel 8 physical 3x
and HTTPS-HLS passes do not qualify this RTSP candidate or override the current
emulator 3x failure. Its five-patch initializer and compiled labels are historical.

### FFmpeg 5 Android integration — platformrate-r3

#### Build and source identity

`ffmpeg-5.0` names this migration series; the actual FFmpeg baseline is **5.1.10**.
Use `init-android-ffmpeg5.sh`; the old `init-android.sh` is the FFmpeg 4.3 path.
The initializer pins [FFmpeg ff87490262107e004f6a16765484e36e1973d705](https://github.com/CarGuo/FFmpeg/commit/ff87490262107e004f6a16765484e36e1973d705),
OpenSSL 3.5.9 (`45e844fa2a14ec92d146bd8f5778ac130b6625fb`), and the existing
libyuv/SoundTouch dependency commits. It reverse-checks all five expected FFmpeg
patches without applying them again. The discovery branch may advance, but an
unexpected existing checkout is preserved and rejected rather than reset.

The tested build environment is Linux x86_64 with Android NDK r22b
(`22.1.7171670`), Bash, Git, Perl, GNU Make, `sha256sum`, and NASM or YASM.
Use a new working directory, with sufficient disk space for source and objects:

```sh
git clone --branch ffmpeg-5.0 https://github.com/CarGuo/ijkplayer.git ijkplayer-ffmpeg5
cd ijkplayer-ffmpeg5
./init-android-ffmpeg5.sh
export ANDROID_NDK=/absolute/path/to/android-ndk-r22b
export FFMPEG5_SOURCE="$PWD/extra/ffmpeg5"
export OPENSSL_SOURCE="$PWD/extra/openssl-3.5.9"
for abi in arm64 armv7a x86_64; do
  android/contrib/compile-ffmpeg5.sh "$abi" || exit
  (cd android && ./compile-ijk.sh "$abi") || exit
done
```

The build script explicitly selects `config/module-lite-more.sh`. ARMv7 retains
API 16; ARM64 and x86_64 retain API 21. The player, SDL and J4A modules end with
`-fno-fast-math`, preserving finite clock checks despite inherited optimization
flags. Keep FFmpeg/player/SDL as a matched three-library set for each ABI.
Read [patch provenance](../patches/ffmpeg5/README.md) and the existing
[dependency/license notices](FFMPEG5_HISTORY.md#tls-behavior-and-dependency-notices).

An optional `FFMPEG5_SOURCE_REPOSITORY=/absolute/local/repository-or-bundle`
override selects a local source containing the exact current pin. It does not
replace dependency pins or permit a different FFmpeg revision. The initializer
checks and the public remote commit/blob checks are separate from a fresh full
network initialization; that full fresh initialization was not rerun for this
publication. Shell syntax and pin/profile/source equivalence were checked.

#### Integration changes

- Preserve codec EOS draining, actual-picture fallback readiness, output-ready
  audio clocks and MOV/HEVC seek replay from the recovered candidate.
- On an audio-capable Android API 23+ AudioTrack path, positive playback-rate
  commands use platform PlaybackParams. Requested and acknowledged speed/pitch
  tuples are separate; read-back must agree before the applied clock changes.
  Pitch preservation requests use pitch 1; non-preserving requests use the
  corresponding rate as pitch. Device rejection remains an explicit error.
- Size the platform AudioTrack at eight times its minimum byte buffer and record
  requested/effective/capacity frames with checked conversion. API 23 exposes
  effective size; capacity read-back is API 24+. This covers the observed Pixel 8
  3× buffer requirement, not every device's possible minimum.
- Pace video and audio-drained video tails in wall time at the acknowledged rate.
  Preserve continuity through seek, pause/resume and return to 1×. The conservative
  queued-audio deadline uses wall-time units at the applied rate and rebases only
  a still-future, same-owner deadline. Terminal played-frame, queue, serial, epoch
  and error checks remain required for completion; 1×/legacy arithmetic remains.

The API <23 branch is preserved. Non-1× playback with its inherited head-clock
backend remains unsupported/unqualified; this work does not promise expanded
rate support on older Android. Video-only and other backend routes are not newly
qualified by the audio-path results.

#### Physical verification and limits — 2026-10-04

The exact ARM64 platformrate-r3 libraries were tested on Pixel 8/API 35 with
**4096-byte system pages** using the real GSY public player/manager path:

- Mux HTTPS-HLS: valid AVC/AAC stream prepared, rendered and advanced normally.
- 0.25×, 0.5×, 1×, 2×, 3×: all passed original bracketed media-position versus monotonic
  elapsed-time and remaining-duration completion assertions. Setter/getter echo
  was not a success criterion. 3× measured 2.97348 with 193.667 ms completion residual
  against 550.333 ms tolerance; 2× measured 2.01517.
- Focused transitions 2/2: 1→3→1, pause/resume, seek/replay and 0.5× audio-drained
  video tail. Tail7 passed all seven unchanged 1× EOF/clock cases. Widget3 passed
  all three unchanged actual GSY cases.

The original Apple sample host did not resolve. Its six-of-seven report remains
an overall historical failure; the unavailable address is excluded from the
current feature assessment. Mux independently qualifies ordinary HTTPS-HLS.
Playback success is not comprehensive TLS/security or audible pitch-quality
certification. Longer sessions and broader vendor/device coverage remain limited.

All three ABIs passed source/build/link/static checks. ARMv7 and x86_64 have no
new physical-runtime qualification here. ARM64/x86_64 ELF LOAD alignment is 16 KiB;
ARMv7 is 4 KiB. Static alignment and the 4 KiB Pixel run do not prove 16 KiB runtime.
See [native identities](platformrate-r3-native.json) and
[GSY package verification](https://github.com/CarGuo/GSYVideoPlayer/blob/ffmpeg-5.0/doc/ffmpeg-5.0.md).
No tag or stable-release guarantee is implied by this branch publication.

#### Compiled source versus publication identity

The carried FFmpeg binaries were compiled from recovered commit
`0cb0a06ef3d2b8e2814cb0e60be99d7986e1bde1`; published ff87490262107e004f6a16765484e36e1973d705 has identical
production FFmpeg code and the additional current documentation. The effective
IJK code is the recovered source plus the reviewed head-clock/platformrate-r3
changes in this branch. The compiled native label remains
`v0.8.8-carguo-35-gd5c0b4eb-platformrate-r3`, rather than being rebuilt just to
embed the new publication commit. Source hashes, not that older base label alone,
bind the effective compiled inputs.

[Compiled project-source hashes](platformrate-r3-compiled-source.json) cover 793
project-owned inputs: 791 recovered tracked inputs plus two new headers. The full
build input receipt additionally records 222 generated dependency/version inputs,
which are intentionally not copied into this repository as new source files.
Generated `ijkversion.h` is produced by the build; dependencies are initialized
at their original pins. Existing tracked symlinks retain their raw relative
Git targets. A publication clone's documentation/initializer pin changes do not
make already-built binaries bit-identical to a fresh build; toolchain paths,
version metadata and build environment remain relevant to reproducibility.

The older [migration/build history](FFMPEG5_HISTORY.md) and
[2026-10-02 recovery record](FFMPEG5_RECOVERY.md) are retained with their original
bounded results. They are historical checkpoints, not current unresolved gates
or transferable pass counts. New functional results do not constitute a new
memory-safety audit.

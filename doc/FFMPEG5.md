# FFmpeg 5 Android integration: RTSP candidate

## Current source and initializer

`ffmpeg-5.0` is the migration branch name; the upstream baseline is **5.1.10**.
The current RTSP work is based on published IJK
`bb1d1b9fa07019155c88fe69fea9db896a108d0f` and FFmpeg
`ff87490262107e004f6a16765484e36e1973d705`, with the frozen RTSP changes added.
The exact RTSP initializer pin is `c157591220febd96fdcffb05283f63dd38627081`.
The previous five-patch pin remains a historical identity, not the current RTSP
source. Do not reset an older checkout in place; use a separate workspace.

The initializer reverse-checks all six patches and never reapplies them or resets
an existing checkout. OpenSSL 3.5.9, libyuv and SoundTouch pins, the explicit local
repository/bundle override, and wrong-revision/non-repository preservation remain
unchanged. The build receipt now includes the sixth patch hash. The initializer
regression keeps fifth-patch preservation and adds sixth-patch preservation of
`rtsp.c`, `rtsp.h` and `rtspdec.c`. **The full integration test at the new pin and
a fresh full remote initialization were not rerun.** Syntax/AST checks do not
replace these tests. See [initializer test scope](../tests/ffmpeg5/README-initializer.md)
and [patch provenance](../patches/ffmpeg5/README.md).

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

The [live-policy host tests](../tests/ffmpeg5/README-rtsp-live.md) include a bounded
20-minute software-consumer soak. Neither local queue age nor the emulator
measurements below establish an end-to-end/camera-to-screen latency SLA.

## Current emulator evidence: 2026-10-05

The final prospective RTSP gate passed **12 executed cases**. One MediaCodec case
was explicitly **N/A**, not passed. Scope: API 29, x86_64 software emulator,
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

The current RTSP candidate has **not been tested on a physical device**.
Optional external HTTPS/HLS smoke was not run in this emulator gate. There is no
new physical MediaCodec, ARM runtime, 16 KiB-page runtime, real-camera/TLS/UDP/H.265,
audible-quality or full A/V-sync qualification. This branch is **not a stable
release or universal 3x playback qualification**.

## Current native build identity

The carried RTSP candidate uses the local compiled snapshot label
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

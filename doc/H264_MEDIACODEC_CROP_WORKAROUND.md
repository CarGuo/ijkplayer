# Opt-in H.264 MediaCodec crop rebuild fallback

This is a review candidate for devices which keep rendering an old H.264 crop
without sending a usable output-format event. It builds on the output-size
handling in [PR #2](https://github.com/CarGuo/ijkplayer/pull/2); it does not replace
that fix or update the libraries in GSYVideoPlayer PR #4264.

`mediacodec-h264-crop-workaround` defaults to **0**. Keep it off unless the
affected stream/device needs it and has been validated. The available evidence
does not establish unrestricted seek, loss-free lifecycle transitions or A/V
compatibility across devices.

## What changes

The asynchronous IJK H.264 MediaCodec input path recognizes a narrowly validated
crop-only SPS change in a complete source access unit. It holds the IDR access
unit, drains the old decoder generation using the existing private EOS path,
recreates the codec with the new description, then queues the held unit once
with its original PTS/DTS. It does not repair pixels by changing only the size
callback.

The input must contain one complete SPS/PPS pair before one IDR slice. PPS bytes
and SPS payload bits other than crop must remain equal. The parser accepts a
bounded progressive 8-bit 4:2:0 subset, Annex B or avcC with 1-, 2- or 4-byte NAL
lengths. It rejects unsupported framing/parameters, fields, FMO, redundant
pictures, multiple-slice admission and IDR discard of prior output. It validates
headers, not entropy-coded pictures. Complete demux access units are required.

The option does not enable hardware decoding by itself. HEVC, software decoding
and synchronous MediaCodec input are outside its admission path. Existing
decoder selection and unrelated options are not changed.

## Seek, Surface and failure boundaries

- After a committed crop change, a real input flush marker stages restoration
  of the immutable pre-workaround description. Codec parameters, CSD, BSF and
  actual codec configuration are restored consistently before the same new-epoch
  source unit is reconsidered. This fixes the tested backward seek to an initial
  headerless A IDR whose parameter sets were supplied only in initial avcC.
- A seek flag alone does not trigger restoration. Ordinary playback/seek before
  any crop commit gets no new restoration reconfiguration. A pending uncommitted
  transition is canceled across an epoch change.
- This is baseline restoration, not a history map or knowledge of an arbitrary
  seek destination. A later headerless B target or a live continuation may not
  identify its required parameters. Arbitrary seek is not qualified.
- Baseline restoration can use the existing dummy codec when no Surface is
  present. A new crop transition still waits for a real Surface. An EOF-only
  new epoch cancels pending restoration and follows ordinary EOS.
- Stop/release/configuration failures remain errors; metadata and BSF state are
  not falsely committed after a failed configure. There is no claim of continued
  playback after a failed codec teardown.

### Coexistence with GSY resolution handling

Keep `mediacodec-handle-resolution-change=1` when the caller already uses it.
Validated `NEW_EXTRADATA`, including an identical accepted description, transfers
ownership to the existing resolution-change path. Pending restoration and its
obsolete baseline snapshot are retired so a later flush cannot roll C back to A.
Any source `NEW_EXTRADATA` stops future crop admission, including short side
data ignored by the legacy updater. Only an accepted description of at least
seven bytes also cancels pending restoration and retires the baseline snapshot.
Short ignored side data does not transfer ownership; malformed-description error
behavior is retained.

Unsupported source parameters or an authoritative description change stop new
crop admission for this pipeline. The stop is sticky: this is not automatic
reactivation after every subsequent description change. The old resolution path
still operates when enabled. Allowing both options does not make that old path
loss-free; see the failed tests below.

## Build and enable

Use the FFmpeg 5 branch's [build instructions](FFMPEG5.md), current initializer
and dependency pins. `ffmpeg-5.0` is the branch name; the FFmpeg baseline is
5.1.10. Do not use the old FFmpeg 4.3 initializer.

For an initialized checkout, the relevant ARM64 build commands are:

```sh
export ANDROID_NDK=/absolute/path/to/android-ndk-r22b
export FFMPEG5_SOURCE="$PWD/extra/ffmpeg5"
export OPENSSL_SOURCE="$PWD/extra/openssl-3.5.9"
android/contrib/compile-ffmpeg5.sh arm64
(cd android && ./compile-ijk.sh arm64)
```

Build and package matching `libijkplayer.so`, `libijksdl.so` and `libijkffmpeg.so`
for each ABI. This patch adds checked SDL operations, so replacing only the
player library while retaining an unrelated old SDL is not supported. No
FFmpeg patch or initializer pin is changed here. The validated build used NDK
r22b, ARM64/API21 and the existing FFmpeg binary; this is not a fresh-build or
runtime qualification of all other ABIs.

Set the option **before preparing a new player**, on an already configured
asynchronous IJK MediaCodec H.264 path:

```java
ijkMediaPlayer.setOption(IjkMediaPlayer.OPT_CATEGORY_PLAYER,
        "mediacodec-h264-crop-workaround", 1L);
```

The option has no effect on another backend or synchronous input. Do not change
an application's backend/synchronous choice solely to hide an unsupported case.
Set it to `0` before preparing a fresh player to disable it.

GSY already applies caller `VideoOptionModel` entries after its hardware defaults.
Preserve the existing list rather than replacing it with a one-element list:

```java
// manager is the existing GSYVideoBaseManager used for the next player.
List<VideoOptionModel> previous = manager.getOptionModelList();
List<VideoOptionModel> options = previous == null
        ? new ArrayList<VideoOptionModel>()
        : new ArrayList<VideoOptionModel>(previous);
for (Iterator<VideoOptionModel> it = options.iterator(); it.hasNext();) {
    VideoOptionModel option = it.next();
    if (option != null
            && option.getCategory() == IjkMediaPlayer.OPT_CATEGORY_PLAYER
            && "mediacodec-h264-crop-workaround".equals(option.getName())) {
        it.remove();
    }
}
options.add(new VideoOptionModel(IjkMediaPlayer.OPT_CATEGORY_PLAYER,
        "mediacodec-h264-crop-workaround", 1));
manager.setOptionModelList(options);
```

Use the standard `java.util` collections and GSY's existing model/manager types.
No new Java API, GSY default, or GSY binary replacement is part of this PR.

## Host validation

See [portable host regressions](../tests/ffmpeg5/h264-crop/README.md).
The published host inputs are newly generated from local FFmpeg lavfi patterns
using the included `generate_fixtures.py`. The generator records commands,
versions and hashes and retains playable intermediates for local experiments.
These inputs are not the historical hardware-test media, whose original
generation recipe was not recovered and whose bytes are not published here.
With a native host build of the matching FFmpeg 5.1.10 source:

```sh
FFMPEG_SOURCE_DIR=/path/to/patched/ffmpeg5 \
FFMPEG_BUILD_DIR=/path/to/native-host-build \
OUTPUT_DIR=/tmp/ijk-h264-crop-tests \
bash tests/ffmpeg5/h264-crop/run-host.sh
```

Normal and ASan/UBSan executions passed the original, baseline-epoch,
resolution-coexistence and independent checks. These are overlapping observations,
not a count of independent end-to-end device tests. They use real FFmpeg helper
and BSF code with deterministic fake codec/JNI/SDL operations. Retained FFmpeg
static libraries were not fully sanitizer-instrumented; leak detection and
hardware concurrency certification are not claimed. The full repository test
suite was not reported as passing.

## Hardware evidence and failures

The frozen ARM64 source was exercised on Motorola edge40 / Android14 with actual
`c2.mtk.avc.decoder` and `c2.mtk.hevc.decoder`, without software fallback.
The 15-method batch had **11 acquisition passes and four failures**. All 88
captured PNGs and 15 report JSONs were reconstructed with SHA checks and the PNGs
were individually inspected. A later exact seek3500 OFF/ON control completed
both acquisition methods and produced 14 more verified, inspected PNGs.

| Scenario | Observation |
|---|---|
|Continuous A/B/C/A crop with B frames, ON|Correct sparse visible-region samples; three matching drain/commit/replay chains.|
|Same candidate, OFF|B/C retained the old A crop, providing a negative control.|
|Ordinary AVC ON/OFF and HEVC ON|Completed with coherent sampled images on actual MediaCodec decoders.|
|Aligned seek; Surface+aligned seek; Surface-only|Later B/C/A samples correct. Invalidated proxies around Surface replacement prevent a loss-free claim.|
|Committed B then seek back to initial headerless A|Actual codec flush, baseline restoration and correct A samples, followed by B again.|
|Requested seek3500|Both OFF and ON showed blocky/overlaid, incorrectly cropped B images. ON later recovered C/A. The option need not be enabled for this sampled problem to occur.|
|True NEW_EXTRADATA to coded-resolution C, ON/OFF|Both reached completion, but the B@5s target instead captured C near 6s. The old immediate reconfigure path invalidated pending B output proxies. These are failures.|
|Planned B→seek→NEW_EXTRADATA C, ON/OFF|The old 5.5s action window was missed after C advanced; the guard rejected the action before seek. The intended crossing was not exercised.|

Candidate OFF is the same new binary, not a genuine PR2 binary. Genuine PR2
same-condition controls remain unexecuted. A corrected early-B crossing
ON/OFF harness subsequently completed both JUnit acquisition methods (17 seconds),
but its pictures and native chronology are still awaiting independent review.
That acquisition result is not yet acceptance of the intended crossing. Source comparison
shows the immediate NEW_EXTRADATA recreation mechanism predates this patch;
that alone does not prove identical original-PR2 hardware behavior.

The first actual raw post-seek AU/NAL identity was not recorded. Do not infer a
proven non-IDR/missing-header landing from the requested seek time. Sparse
screenshots and zero logged owned-buffer counts do not establish all-frame
delivery. Audio was disabled, so audio continuity and A/V sync remain untested.
Three small stage-log records were incomplete at the source; all 17 complete
reports and 102 PNGs were unaffected. No historic automatic pixel oracle was
used. This is one hardware family and OS version, not universal qualification.

## Suggested application verification

1. Use a separate test build with matched native libraries. Preserve the existing
   backend, renderer, fit mode, resolution-change setting and other options.
   Compare fresh players with the new option OFF and ON on the same device/media.
2. Confirm the actual decoder through `getMediaInfo()` and native logs. Merely
   selecting a decoder name or seeing advancing time does not prove hardware
   playback or correct pixels.
3. Check the visible region before and after each crop transition, with B-frame
   material and A→B→C→A changes. Retain actual pictures as well as size callbacks.
4. Check ordinary AVC/HEVC, pause/resume, backward seek to the initial region,
   aligned and nonaligned seeks, Surface replacement, real resolution changes
   and EOF. Include audio/A/V synchronization if the application uses audio.
5. For the missing crossing, require a committed/replayed B before the actual
   seek, a real flush, then authoritative C NEW_EXTRADATA in the new epoch, with
   no stale A restoration overriding C. C appearing before seek or an ambiguous
   native order is not successful coverage. Do not move a missed target and
   relabel the old failed test as passed.

The existing native messages include `H264 crop boundary`, `H264 crop drained`,
`H264 crop generation commit`, `H264 crop replay` and baseline restoration.
Keep case boundaries, timestamps and codec/serial associations when reviewing
them. Remove private media URLs, local identifiers and unrelated logs before
posting diagnostics publicly. Do not clear user data to perform these checks.

Leave this opt-in behavior disabled on combinations that have not met the
application's acceptance criteria. The known limits above remain relevant even
when a JUnit acquisition method passes.

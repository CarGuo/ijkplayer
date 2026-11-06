# Reproducible synthetic regression inputs

All nine binary inputs in this directory were generated afresh by
[`generate_fixtures.py`](../generate_fixtures.py), using only the locally installed
FFmpeg `lavfi` sources `testsrc2` and `color`. No external media, old test-harness
media, downloaded sample, device capture, or old derived packet was used as an
input. No third-party source media is redistributed. FFmpeg and libx264 are tools
used to encode these generated geometric patterns and flat colors; they are not
included in this repository change.

## Regenerate from scratch

Requirements beyond the host runner: installed `ffmpeg` and `ffprobe`, FFmpeg
`lavfi`, `testsrc2`, `color`, the `libx264` encoder, MP4/H264 muxers and demuxers,
and `h264_metadata` / `filter_units` bitstream filters. No network is used.

```sh
python3 tests/ffmpeg5/h264-crop/generate_fixtures.py /tmp/new-ijk-crop-fixtures
```

The destination must not exist. Override `FFMPEG` and `FFPROBE` if needed. The
generator writes:

- `fixtures/`: the nine inputs plus manifest and portable command provenance.
- `generation.json`: the exact command argument lists, tool versions, verified
  dimensions and every ordinary packet's size, hash and actual PTS/DTS/duration.
- `work/`: locally generated playable intermediate media, including the
  12-second ordinary MP4, three-second C MP4 and single-picture A/B H264 streams.
  These are new reproduction materials, **not** historical real-device media.
  No new player or device framework is required or included.

`manifest.json` pins the checked-in bytes and generator hash. `generation.json`
beside this README records the commands that produced them, with `${OUTPUT}` as
a portable output-directory placeholder. The recorded tool is FFmpeg 7.1.5
with its installed libx264. Encoder versions can produce different valid bytes;
do not assume cross-version hash identity. The same local toolchain produced
byte-identical inputs in an independent repeat generation.

To run on a regenerated corpus, preserve the tracked README, copy the contents
of the new `fixtures/` directory into this directory, and run `../run-host.sh`
with the matching FFmpeg 5.1.10 host dependencies as documented in the parent
README. The manifest changes with the corpus; the production implementation and
test safety assertions do not need changes.

## Input construction and checks

- `actual_a.h264`: one real single-slice High-profile `testsrc2` IDR at 640x360,
  coded at 640x368, medium/CRF 18, one encoder thread, closed GOP 96 and two B
  frames configured. The historical filename is retained for harness
  compatibility; its bytes are newly generated.
- `actual_b.h264`: that newly generated A picture transformed with
  `h264_metadata=crop_bottom=96`, yielding 640x272. All non-SPS NALs are verified
  byte-identical to A. This changes cropping, not coded resolution.
- `avc3/first_A.avcc-au`: the new A AU with only SPS/PPS removed, then encoded
  with four-byte NAL lengths. `boundary_B.avcc-au` contains the new complete B AU
  with four-byte lengths. These are packet-level host-test inputs, not a claim
  that this generator builds a multi-description avc3 container.
- `small_a.h264` / `small_b.h264`: a newly encoded 32x32 black IDR and its 32x24
  crop. Removing encoder SEI keeps every NAL below 256 bytes and each AU below
  200 bytes. This preserves the real 1-byte-length test, without truncating
  picture payloads or weakening any assertion.
- `ordinary_video_packets.bin`: all 288 packets of a new 12-second 640x360
  `testsrc2` MP4 at 24 fps, with two B frames and closed GOP 96. PTS, DTS and
  duration come from actual MP4 demuxing in a 1/90,000 time base; timestamps are
  not invented. Packets 0, 96 and 192 are checked to contain source SPS/PPS/IDR.
  Every packet is converted to Annex B. Records contain a little-endian uint32
  byte count, three little-endian int64 values (PTS, DTS, duration), then that
  many AU bytes. No audio is stored.
- `resolution/C_480x272.avcc` / `first_C_480x272.avcc-au`: a fresh three-second
  480x272 `testsrc2` MP4 provides its real avcC and first IDR. In-band SPS/PPS are
  removed from the saved first C AU, so the new description is required. This
  is a real coded-size change and not an SPS crop substitute.

The generator validates all declared dimensions and performs full software
decode checks of A, B, small A/B and C. The separate regression runner tests the
unchanged production helpers using real FFmpeg 5.1.10 libraries, in normal and
ASan/UBSan modes. System FFmpeg generation/software decode is not evidence of
FFmpeg 5 container demux behavior or Android hardware rendering.

# MediaCodec packet and HEVC parameter-update host regressions

Run against a native build of the matching patched FFmpeg 5.1.10 source:

```sh
./run_ffamc_ffmpeg_tests.sh /path/to/ffmpeg5 /path/to/ffmpeg5-host [output-directory]
SANITIZE=1 ./run_ffamc_ffmpeg_tests.sh /path/to/ffmpeg5 /path/to/ffmpeg5-host [output-directory]
```

The host build needs static avformat/avcodec/avutil, the MOV demuxer and file
protocol, H.264/HEVC decoders/parsers and both mp4toannexb filters. A host
`ffmpeg` with libx264/libx265 and lavfi generates synthetic inputs; `python3`
muxes the HEVC update fixture. `FFMPEG_BIN` and `CC` select those tools. Input
fixture generation does not use the tested FFmpeg libraries.

The runner includes the existing conversion, queue, input lifecycle and H.264
extradata tests, plus `ffamc_hevc_parameter_update_test.c`:

- Real FFmpeg 5 decoder pixel checks for in-band VPS/SPS/PPS updates, PPS-only
  updates, separate parameter packets, updates after an IRAP and later IRAPs
  without repeated parameter sets
- `AV_PKT_DATA_NEW_EXTRADATA` without in-band parameters, conversion of outgoing
  side data to Annex B, persistence, NAL-length changes, side-data-only packets
  and initialization without original hvcC
- Transactional rollback after malformed packet/side data, input ownership,
  EAGAIN, EOF and repeated seek/flush reset to the initial CSD and NAL length
- Structural tests for 1/2/4-byte NAL lengths, multiple parameter-set IDs and
  layers, replacement of an existing ID, emulation prevention, six sublayer
  profiles, truncated SPS identifiers, invalid IDs and malformed hvcC
- 4096 original hvcC SEI NALs retain their order through updates and flushes;
  hvcC sizing uses two passes and SEI storage uses one cache block
- Existing Annex B and absent-extradata passthrough

The structural parameter sets deliberately contain only enough syntax to test
identifier extraction. They are not presented as decodable multilayer videos.
Real pixel tests use independently encoded 64x48 HEVC pictures whose only
encoding-setting difference is PPS sign-data-hiding. Hashes are derived from
the native original packets, not hard-coded to one host encoder version.

`make_hevc_parameter_update_fixture.py OLD.mp4 NEW.mp4 OUTPUT.mp4` creates a
real three-frame `hev1` MP4 with unchanged old hvcC, an old IDR, a new IDR with
in-band parameters, and a later new IDR without parameters. It validates its
one-frame input shape and rewrites sample sizes, chunk counts/offsets and
durations. The host runner reads that muxed file and verifies all three decoded
luma hashes after filtering. The later IRAP case also fails with a same-packet
injection-order-only fix.

`SANITIZE=1` instruments the test translation units and the production HEVC
filter, then links the supplied FFmpeg libraries. Fully instrument those
libraries separately for coverage of their internals. If LeakSanitizer is
unsupported by a ptrace-based executor, use `ASAN_OPTIONS=detect_leaks=0` and
record that leak detection was not run. Do not disable ordinary assertions.

These are host packet/decoder tests. They do not establish Android MediaCodec,
physical vendor decoder, resolution-reconfiguration or sustained-playback
acceptance. An already-Annex-B stream still passes through; the filter cannot
infer NAL lengths from a length-prefixed stream without hvcC or valid new
extradata. Flush starts a new epoch at the original CSD; demuxers must resend
applicable configuration changes when seeking into an updated configuration.

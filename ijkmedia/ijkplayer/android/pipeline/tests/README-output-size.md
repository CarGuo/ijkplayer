# MediaCodec output-size regressions

Run independently of FFmpeg/JNI and a physical decoder:

```sh
bash run_ffamc_output_size_tests.sh /tmp/ffamc-output-size
SANITIZE=1 bash run_ffamc_output_size_tests.sh /tmp/ffamc-output-size-sanitized
CFLAGS='--coverage -O0' bash run_ffamc_output_size_tests.sh /tmp/ffamc-output-size-coverage
```

The tests compile the production output-size adapter and visible-size resolver
against minimal pipeline dependency types. They exercise H.264 ad -> content ->
ad with unchanged 1920x1080 allocation and a 1280x536 visible crop, repeated and
single-axis changes, offset crops, inclusive pixel bounds, rotation, absent/partial/
invalid geometry, integer boundaries, unchanged HEVC validation, other codecs
and the retained case-insensitive `OMX.TI.DUCATI1.` H.264 workaround. The main
`run_ffamc_ffmpeg_tests.sh` runner also invokes these tests.

Both MediaCodec output-dequeue paths use this adapter. Valid H.264 output sizes
update `frame_width/frame_height`, which reach the existing per-frame size
notification; no new extradata, codec recreation, buffer reallocation or manual
notification is needed. Invalid H.264 output geometry keeps the last known
size instead of introducing a new playback error. HEVC's strict validation
is unchanged.

These are deterministic host regressions, not Android hardware acceptance. A
device regression should retain real MediaCodec decoding, play through a crop
change with unchanged allocation, and verify the reported video dimensions,
rendered aspect ratio and advancing playback. For example, 1280x536 square-pixel content
fits a 1920x1080 viewport as 1920x804 with 138-pixel top/bottom margins. Do not
infer all vendors, codecs, Surface/Texture modes or TI compatibility from one
device.

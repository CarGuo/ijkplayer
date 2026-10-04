# Native ELF packaging inspection

`check_native_outputs.py` is adapted from the restored historical verifier. It
reads exactly libijkffmpeg.so, libijksdl.so and libijkplayer.so in each of the
arm64-v8a, armeabi-v7a and x86_64 module output directories. It does not load them.
GNU readelf and strings must be available on the host.

Run from the restored workspace:

```sh
python source/ijkplayer/tests/ffmpeg5/test_native_outputs.py
python source/ijkplayer/tests/ffmpeg5/check_native_outputs.py source/ijkplayer \
  --output checkpoints/native-artifacts/native-output-inventory.json
```

Checks include exact output set, ELF little-endian class/machine/ET_DYN identity,
SONAME, LOAD power-of-two alignment, file-offset/virtual-address congruence,
file/memory sizes, expected dependencies, FFmpeg dynamic imports resolved by the
bundled FFmpeg exports, and exact embedded FFmpeg/OpenSSL identity strings.
Identity defaults are n5.1.10 and OpenSSL 3.5.9 29 Sep 2026; explicit CLI arguments
allow intentional later version changes. SHA256, sizes, symbols and layouts are
recorded in JSON. The original smart-fallback option-presence check is retained.

arm64-v8a and x86_64 require LOAD alignment >=16384 and RELRO end alignment at
16384. armeabi-v7a retains a 4096 baseline. RELRO rounded extent is checked against
the reserved image span; dense LOAD coverage is recorded separately because
inter-segment gaps are not by themselves a failure of this range check. These
checks do not prove minimum Android API; actual Application.mk/build evidence
must document API16 ARMv7 and API21 64-bit targets.

The offline fixture suite exercises positive and negative parsing, bad offsets,
bad sizes/alignment, RELRO missing/outside/unaligned, dependency omissions and
unexpected external shared libraries. Synthetic fixtures are not Android runtime
results. None of these checks certify runtime loading, devices/vendors, JNI,
codecs, HTTPS/HLS, playback speed, broad safety, or security properties.

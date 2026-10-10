# ARMv7 OpenSSL optimizer regression

The Android builder adds `-fno-unroll-loops` to OpenSSL Configure only for
`armv7a` with compiler macro `__clang_major__ == 11`. The upstream OpenSSL 3.5.9
source, NDK version, API 16 minimum, and TLS verification behavior are unchanged.
The flag participates in the existing content-addressed TLS cache identity.
ARM64 and x86_64 receive no additional flags.

## Compiler evidence and historical direct-HTTPS result

With NDK r22b Clang 11.0.5, compiling `crypto/sparse_array.c` with `-O3` emits a
sparse-array traversal descent block that reloads the next stack entry before
storing the new node there. The optimized LLVM IR retains store-before-load
ordering. `-O2` and `-O3 -fno-unroll-loops` do not emit that ordering inconsistency.
`-fno-strict-aliasing` and disabled vectorization do not resolve it.

The direct-HTTPS evidence below belongs to the earlier ARMv7-only candidate,
built before the HLS inheritance fix. The current combined candidate pins
FFmpeg `0c8735b51d29dbc74e18c235246d5230bd5db989` and rebuilds all three migrated
ABIs. Its current ARMv7 tuple has a separate
[Pixel 5 / API 30 TLS/HLS result](https://console.firebase.google.com/project/test-58c3b/testlab/histories/bh.faa1330aa1b29782/matrices/8558173587236303850/executions/bs.74fab5dea7b801b/test-cases)
(2026-10-10, test-project access required): 15 passed and 1 failed, with the sole
failure reproducing the original ARMv7 CA-load crash. The candidate passes
direct HTTPS, strict certificate/peer-name rejection, nested-segment wrong-CA/SAN
rejection and controlled HLS audio at 1x/3x. Official sparse-array, property and
user-property ARMv7 source tests also pass with complete TAP, in separately
linked static-libcrypto executables rather than against dynamic IJK. The custom
invariant probe below is not covered by that official-unit claim. Current ARM64
and x86_64 tuples remain build/static-only. See the
[current qualification limits](../../../doc/FFMPEG5.md#current-tls-fixes-and-qualification-boundary).
Do not reuse the older hashes below or assume historical passes qualify changed
binaries.

The targeted workaround repaired the observed ARMv7 certificate-loading crash
in the 2026-10-10 [Pixel 3 / API 28 comparison](https://console.firebase.google.com/project/test-58c3b/testlab/histories/bh.93e2df2eab2c5575/matrices/7759948617160091666/details)
(test-project access required). The same runtime and valid CA reproduced the
original library's SIGSEGV at address `0xa` after CA loading began (exit 139),
while the candidate returned `Loaded=1` and exit 0. The Java/JNI playback process
was verified as 32-bit. Candidate `IjkMediaPlayer` completed the two-second AAC
fixture over owned loopback TLS 1.2 with default options and with `tls_verify=1`.
Wrong-CA, DNS-name and IP-name cases rejected with the expected certificate
alerts, no successful handshake, no HTTP request and no preparation/completion;
each negative had its own successful same-build trusted prerequisite. The
original ARM64 certificate-load control passed too.

The instrumented suite deliberately reports **8 passed, 1 failed**: the one
failed test is the reproduced original ARMv7 crash, not a passing original
certificate load. This is a bounded TLS regression result, not whole-release
qualification. The test used a two-second AAC/M4A fixture, not HLS or video;
it does not establish TLS 1.3, external DNS routing, all certificate types,
long-session behavior, performance, hardware decoding or 16 KiB-page runtime.
That historical run did not execute the official sparse-array/property tests
or the invariant probe. Current official-unit evidence is scoped separately
above; broader playback and other ABI/device coverage remain separate work.

The tested candidate was built from IJK
`a599f60268312f3093d6f0ca165a3d06c76970cb` plus this build workaround, FFmpeg
`c157591220febd96fdcffb05283f63dd38627081`, and unmodified OpenSSL 3.5.9
`45e844fa2a14ec92d146bd8f5778ac130b6625fb`. The distribution tuple SHA-256 values
are:

- `libijkffmpeg.so`: `a9c7f45f845e48486ae2cb3c3cb72b4529ebfd02be3bb5f666165e590cdc7e0a`
- `libijkplayer.so`: `413c5fb6179501d445ae71701f3c9bdcbd6fb0e26111b5cc10643826e6b48bd7`
- `libijksdl.so`: `8bf24de7fa3d292a7906c71934c2a1e4d9e8b87108eda1293a6cb52e72fcb19a`

Keep the three libraries together. Source publication after the build does not
change their bytes or turn a new source commit into the compiled revision.

## Fast build-guard tests

From the IJK repository root:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tests/ffmpeg5/test_tls_armv7_optimizer.py -v
```

These tests execute the builder's actual flag-selection block with isolated
compiler fixtures. They cover Clang 11 and other versions, Android and Apple
banner formats, non-Clang compilers, all supported ABIs, and flags in the cache
identity. Compiler macros determine the guard; banner formatting is ignored.

## Sparse-array invariant probe

`invariants.c` exercises 21 datasets: empty and single-leaf arrays; dense arrays
spanning one, two, and three levels; every 4-bit index-depth boundary; and the
maximum-index range. Both `ossl_sa_doall_arg` and `ossl_sa_doall` must deliver
all expected index/value pairs exactly once. Callback context identity, `get`,
and element count are checked too. A callback value is compared with a known
owned object before it could be dereferenced.

Use headers and a static archive from the same OpenSSL source/configuration.
For a host build, set `OPENSSL_SOURCE` and `OPENSSL_BUILD` to existing source and
configured host-build directories; put outputs outside the repository:

```sh
OUT=$(mktemp -d)
cc -O3 -Wall -Wextra \
  -I"$OPENSSL_BUILD/include" -I"$OPENSSL_SOURCE/include" \
  tests/ffmpeg5/tls-sparse-array/invariants.c \
  "$OPENSSL_BUILD/libcrypto.a" -ldl -pthread -o "$OUT/invariants"
"$OUT/invariants"
```

For an Android build-only check, use the matching configured Android OpenSSL
build and the NDK `armv7a-linux-androideabi16-clang` compiler, adding
`-fPIE -pie`. Execute the resulting program through the ordinary Android test
workflow. Building an executable is not a runtime pass.

For controlled source-code generation, use the same ARM compiler and headers:

```sh
"$CC" -O3 -fPIC -I"$OPENSSL_BUILD/include" -I"$OPENSSL_SOURCE/include" \
  -S "$OPENSSL_SOURCE/crypto/sparse_array.c" -o "$OUT/baseline.s"
"$CC" -O3 -fno-unroll-loops -fPIC \
  -I"$OPENSSL_BUILD/include" -I"$OPENSSL_SOURCE/include" \
  -S "$OPENSSL_SOURCE/crypto/sparse_array.c" -o "$OUT/candidate.s"
```

Keep compiler versions, complete flags, source hashes, archive hashes, assembly
outputs, runtime output, and exit status with the test report. A host success or
compiler-output difference must not be reported as an Android TLS success.

# Pinned local-recovery initializer test

Run from a delivered IJK tree with the three pinned dependencies already present:

```sh
python3 tests/ffmpeg5/test_initializer.py /absolute/path/to/FFmpeg
```

Requires Bash, Git, Python 3, enough temporary disk space for independent FFmpeg
clones and a standalone bundle, and the delivered FFmpeg repository at
`c157591220febd96fdcffb05283f63dd38627081`. This test does not download dependencies.
It verifies and reuses these existing IJK checkouts through temporary symlinks:

- `extra/openssl-3.5.9`: `45e844fa2a14ec92d146bd8f5778ac130b6625fb`
- `ijkmedia/ijkyuv`: `a0c7dd3e4b095649881f39152655437b8cde1589`
- `ijkmedia/ijksoundtouch`: `6bf39cd3bf6b0c156267d12446b0d6bdcfcd53c2`

The initializer only reads already-pinned dependency checkouts. The test disables
network Git protocols (`GIT_ALLOW_PROTOCOL=file`), creates its FFmpeg checkout
in a temporary IJK skeleton, and checks the original source/dependency revisions
and worktree statuses again at the end. All intentionally destructive test
operations are confined to the temporary copy.

Coverage:

1. Fresh initialization from the actual local repository, exact pin and all six reverse checks
2. Idempotent rerun, with unchanged checkout status
3. Missing fifth patch fails and preserves the modified MOV file
4. Missing sixth patch fails and preserves all three modified RTSP source files
5. Wrong source revision fails and preserves that revision/worktree
6. Non-repository target directory fails and preserves its sentinel
7. Invalid local source override fails before creating a checkout
8. Fresh initialization from a standalone bundle containing the recovery pin
9. Original source, pre-existing FFmpeg checkout and dependency checkouts remain unchanged

## Current verification status

The current initializer requires the RTSP commit and all six reverse checks.
The test retains the fifth-patch removal/preservation regression and adds the
sixth-patch removal/preservation regression. This is more than a pin-only change.
The complete initializer integration test has **not been rerun at the new pin**;
Python AST parsing and shell syntax checks are not an integration pass. A fresh
full remote initialization has not been rerun either. The local-source override,
no-reset/no-overwrite behavior and all three dependency pins are preserved.

The exact six-patch initializer pin is `c157591220febd96fdcffb05283f63dd38627081`.
The test requires a local repository or bundle containing that exact commit,
plus the three pinned dependency checkouts listed above.

See [current RTSP scope and historical R3 results](../../doc/FFMPEG5.md).
Android compilation, emulator playback and physical-device qualification are
separate from this initializer test.

## Historical five-patch verification record

The preceding published initializer used
`ff87490262107e004f6a16765484e36e1973d705` and required five reverse checks.
Its original offline integration results apply to the recovered five-patch
checkpoint. That earlier publication changed only the expected pin and did not
rerun the resource-intensive integration test or fresh full remote initialization.
Those earlier results are retained as history; they do not certify the new
six-patch pin or the added sixth-patch regression.

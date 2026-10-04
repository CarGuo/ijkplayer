# Pinned local-recovery initializer test

Run from a delivered IJK tree with the three pinned dependencies already present:

```sh
python3 tests/ffmpeg5/test_initializer.py /absolute/path/to/FFmpeg
```

Requires Bash, Git, Python 3, enough temporary disk space for independent FFmpeg
clones and a standalone bundle, and the delivered FFmpeg repository at
`ff87490262107e004f6a16765484e36e1973d705`. This test does not download dependencies.
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

1. Fresh initialization from the actual local repository, exact pin and all five reverse checks
2. Idempotent rerun, with unchanged checkout status
3. Missing fifth patch fails and preserves the modified MOV file
4. Wrong source revision fails and preserves that revision/worktree
5. Non-repository target directory fails and preserves its sentinel
6. Invalid local source override fails before creating a checkout
7. Fresh initialization from a standalone bundle containing the recovery pin
8. Original source, pre-existing FFmpeg checkout and dependency checkouts remain unchanged

This verifies local-source recovery with cached dependencies. The pin is published. The original offline results apply to the recovered code
checkpoint; this publication updates the expected pin only and does not claim
a fresh full remote initialization or a rerun of this resource-intensive test.
Android compilation and runtime playback evidence are documented separately.

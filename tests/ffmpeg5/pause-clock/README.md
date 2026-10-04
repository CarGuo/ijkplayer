# Paused audio clock host regressions

From this repository, run:

```sh
python3 tests/ffmpeg5/pause-clock/run.py --out /tmp/ijk-pause-clock-results
python3 tests/ffmpeg5/audio-submission/run_extract.py --out /tmp/ijk-submission-results
```

The first runner uses only Python 3, a host C11 compiler and pthreads. Its default
source is this repository. `--source-root` selects an explicit source tree and
`--overlay-root` optionally supplies a sparse reviewed overlay. Generated source,
executables, logs and source hashes go under the required `--out` directory.
No Android, JNI, device, network or audio service is used.

The public pause request remains asynchronous. Before this repair, a clock
anchored at156 ms at time100.000 returned321 ms at100.165. Dispatching native
pause11 ms later only flipped its paused bit, exposing the old156-ms anchor.
The unchanged absolute100-ms pause assertion then failed. The extracted source
now captures332 ms when that native pause is dispatched and retains332 ms
through the500-ms observation. It does not freeze at the earlier public request
or change the assertion's timing or tolerance.

A late accepted write also needs separate handling. While paused, the same
current-generation ACK still commits real-byte ownership and its modeled output
deadline, but does not overwrite a finite paused audio position. A first finite
timestamp or a new seek generation can initialize that position. The public
getter retains this captured position during an undrained terminal audio
interval even when the EOF model changes after an ACK. Unknown terminal PTS,
drained A/V handoff and automatic video-only stored-frame behavior remain intact.
The snapshot reuses the existing audio Clock; no new public structure is added.

Pause/resume, transactional AudioTrack ACK publication and public position reads
are serialized by the existing play mutex. Lock ordering is play, frame queue,
audio queue, video queue when needed, then EOF snapshot. The AudioTrack latency
query precedes the play lock, and no backend write is enclosed by these locks.
An ACK takes its publication time after waiting for the play lock. This scope is
the negotiated AudioTrack ACK path; legacy callback-only backends retain their
existing clock publication behavior and are not claimed to be universally free
of clock races.

The tests extract unchanged production clock, master selection, pause/start,
public position, submission and EOF functions, the actual asynchronous public
pause request, and the existing test_core.c playback-clock fixture. Controlled
clock and backend dependencies use real pthread mutexes, checked lock order and
one explicit ACK-waits-across-resume thread schedule. There are78 counted
assertions plus the complete existing playback-clock fixture. Normal, O3 with
-fno-fast-math, and ASan/UBSan modes cover:

- Asynchronous dispatch, repeated pause and ACK before/after pause
- Ordinary and terminal paused positions, ownership and latency deadlines
- Pending/failed/new-generation seek, stale ACK and stepped video while audio
  stays paused, including is.paused=0 with audclk.paused=1
- Unknown/nonfinite clocks, first finite initialization and terminal unknown PTS
- Explicit VIDEO/EXTERNAL masters, drained A/V and automatic video-only modes
- Full paused duration excluded from EOF latency and publication after lock wait
- The first ordinary resumed ACK, including its unmodified source correction

After resume, normal accepted-end-minus-latency anchors are retained. For example,
the deliberate long-ACK-gap fixture corrects321 ms to158 ms at its first resumed
ACK. This repair adds no monotonic clock clamp, delay, retry or duration bound.
The existing38 submission/backend regressions keep every prior assertion; their
fixture only adds the play mutex, paused flag and updated lock ranks.

The recorded API35 pause_resume failure did not serialize both exact assertion
operands. These source counterexamples prove defects and their repairs, but do
not establish the precise triggering interleaving in that run. Native builds,
APK provenance and runtime qualification remain separate. LeakSanitizer is
disabled under the host tracer; this is not a leak-check or full-player race test.

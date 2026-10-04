# AudioTrack submission / EOF host regressions

Run from this IJK repository:

```sh
python3 tests/ffmpeg5/audio-submission/run_extract.py --out /tmp/ijk-audio-submission-results
python3 tests/ffmpeg5/audio-submission/close/run_close.py --sanitizers --out /tmp/ijk-audio-close-results
```

Python 3, a host C11 compiler (`cc`) and pthreads are sufficient. No Android SDK,
AVD, audio service, network, or sound output is used. The default source is this
repository's current production source; the main runner's optional `--source-root`
selects another explicitly provided IJK source tree with this interface. Neither
runner silently falls back to a workspace-specific or historical source tree.
Omit `--out` to use temporary host output. Generated headers, executables, logs
and input SHA256s are evidence, not tracked source.

The deterministic runner extracts the complete production EOF helper, callback,
submission owner hook, AudioTrack buffer helper/backend loop, pause setter, and
the actual read-thread output-error/completion branches. Dependency stubs provide
controlled monotonic time, scripted JNI write results and checked mutex ordering.
The separate close probe extracts the actual submission helper and close function
and runs 100 real pthread condition-wakeup/join iterations, with mocked JNI/core
policy. It covers quiet-wait cancellation, not blocking-JNI interruption.

## Contract and counterexample

Before the acknowledgment path, the callback advanced its media endpoint and
cleared `audio_callback_busy` before `AudioTrack.write` ran. A final 256-byte real
buffer prepared at t=10.000 with latency=0.177667 could therefore satisfy EOF at
t=10.200 while its write had accepted zero bytes. Positive short, zero and error
returns were only warnings; the next callback could replace the lost suffix with
silence. This is a source-level counterexample, independent of any recorded
Android timing failure.

The opt-in AudioTrack path stages a serial/token-tagged real buffer. The EOF gate
stays closed until the entire buffer is accepted. Positive short returns retain
and retry the exact suffix. Only full acceptance publishes the staged endpoint
and latency-based deadline. Zero/error results during active delivery produce a
persistent output error, distinct from the demux error variable. The existing
Java API and backends using the original callback are unchanged.

Pause retains the transaction and accepted offset. Failed seek retains the old
generation; successful seek/flush cancels it, and late old acknowledgments cannot
change newer state. A callback that crosses generations refills its output from
the newly decoded frame without losing that frame's unconsumed bytes. A normal
old-decoder/new-queue flush carries the rejected generation, including when its
output is entirely silence. An unexplained same-generation flush is an output
failure, not permission to replay an already accepted prefix.

Synthetic silence does not keep EOF pending. A failed idle-silence write after
the same generation's real drain is complete becomes a quiet condition wait,
not ERROR after COMPLETED or a retry spin. Drain is rechecked under the same
queue/snapshot lock order on every wake. New data after EOF can resume even if
its queue serial did not change. Seek, pause/resume, restart and abort/close also
leave or control that wait correctly.

## Coverage and limits

The 27 deterministic scenarios cover in-flight final writes; multiple real
buffers; exact short suffixes; pause/resume; failed/successful seeks before and
after a full write; stale acknowledgments; abort during write and wait; zero and
negative writes; partial-write errors; generation changes within/at callback
entry; unexplained flush; synthetic predrain failure; postdrain failure before
and after completion; quiet close; restart; same-serial data after EOF; and the
legacy callback path. Three additional NaN/+Inf/-Inf endpoint cases verify that
real buffers still require acknowledgment and retain a finite drain deadline
without turning unknown timestamps into valid media clocks. Lock-rank assertions reject queue/EOF/backend inversions
and confirm writes/control waits do not retain queue or EOF locks.

Normal and optimized host builds plus AddressSanitizer/UndefinedBehaviorSanitizer
are run by the main runner. Leak detection is disabled because traced runners
may not support LeakSanitizer; this is not a leak-check pass. Sanitizers cover
extracted production and test code, not the full Android player or linked codec
libraries. The existing signed/unsigned buffer-size comparison may warn.

Accepted/submitted bytes are not playback-head measurements and are not audible
PCM evidence. The deadline still uses an estimated latency, not physical output
completion. These tests neither prove nor fix the cause of the recorded
3605-ms audio-only run. No container-duration clamp, wall-clock clamp, acceptance
threshold change or timing-oracle relaxation is part of this repair.

An AudioTrack JNI write already blocked inside Android remains subject to the
backend's existing cancellation limits. This repair holds no queue/EOF/backend
mutex across that call and preserves ownership through eventual return; it does
not promise that such a write is promptly interruptible. Android compilation,
real-backend timing, device runtime and audible playback require separate gates.

## Pause state across a cancelled submission

The output helper may issue `AudioTrack.pause()` while a seek is pending. Once
that seek changes the generation, the helper cancels the old submission and
returns to the outer output loop. If resume arrives before the outer loop reads
its requested pause flag, that loop can skip its pause branch. Previously the
helper's local pause state was then lost, allowing the next buffer to reach
`AudioTrack.write()` without an intervening `AudioTrack.play()`.

The last commanded track state now belongs to the entire output-thread call.
Both the outer loop and buffer helper update that same private value. A requested
resume is reconciled before a new callback/write even when the outer pause branch
was skipped. The value is not a queried Android playback state and does not
certify that a Java play/pause call succeeded. No public structure, submission
acknowledgment, generation, EOF deadline, zero/error-write policy or wait changes.

Eight additional deterministic schedules track the issued physical play/pause
state: resume before the outer pause read; resume inside its wait; paused failed
seek retaining its exact suffix; abort during paused seek; abort after stale
cancellation; another pause during the next generation's callback; unexplained
same-generation flush remaining fatal; and two successive paused seek generations.
All original 27 scenarios and three unknown/nonfinite endpoint assertions remain.
The close probe only adds storage for the helper's private state argument.

The saved API35/16-KiB H264 synchronous seek case received a core audio-output
`-EIO` notification, while its MediaCodec loop exited normally during release.
Its raw AudioTrack return and pause/play sequence were not recorded. The missing
play interleaving is independently reproducible in the exact source, but is not
proven to be the trigger of that recorded failure or any earlier timing failure.

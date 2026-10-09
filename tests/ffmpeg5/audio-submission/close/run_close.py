#!/usr/bin/env python3
"""Extract the repository's close/helper functions and test real pthread wake/join.

Only host C compilation and mocked JNI/core policy are used. This does not prove
that an already-blocked Android AudioTrack.write can be interrupted. Sanitizers
disable leak checking because the host tracer does not support LeakSanitizer.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shlex
import subprocess
import tempfile


def extract(text, name):
    start = text.index(name)
    start = text.rfind("\n", 0, start) + 1
    brace = text.index("{", start)
    depth = 0
    for index in range(brace, len(text)):
        depth += (text[index] == "{") - (text[index] == "}")
        if not depth:
            return text[start:index + 1]
    raise ValueError(name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, help="Explicit production source tree; default is this repository")
    parser.add_argument("--out", type=Path, help="Write generated files here; default is a new temporary directory")
    parser.add_argument("--sanitizers", action="store_true", help="Also run AddressSanitizer and UndefinedBehaviorSanitizer; leak detection is disabled")
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    repo = (args.source_root or here.parents[3]).resolve()
    out = (args.out or Path(tempfile.mkdtemp(prefix="ijk-audio-close-"))).resolve()
    out.mkdir(parents=True, exist_ok=True)
    backend = repo / "ijkmedia/ijksdl/android/ijksdl_aout_android_audiotrack.c"
    header = repo / "ijkmedia/ijksdl/ijksdl_audio.h"
    backend_text = backend.read_text()
    names = ["static int aout_reset_playback(", "static int aout_bind_generation(", "static int aout_device_failed(", "static int aout_accept_prefix(", "static int aout_write_submitted_buffer(", "static void aout_close_audio("]
    (out / "extracted.h").write_text("\n\n".join(extract(backend_text, name) for name in names) + "\n")
    audio = header.read_text()
    start = audio.index("typedef struct SDL_AudioSubmission")
    end = audio.index("typedef struct SDL_AudioSpec", start)
    aout_header = repo / "ijkmedia/ijksdl/ijksdl_aout.h"
    rate_flags = re.search(r"enum \{ SDL_AUDIO_RATE_SPEED =.*?\};", aout_header.read_text(), re.S).group()
    (out / "submission.h").write_text(rate_flags+"\n"+audio[start:end])
    playback = repo / "ijkmedia/ijksdl/ijksdl_audio_playback.h"
    (out / playback.name).write_text(playback.read_text())
    (out / "probe.c").write_text((here / "probe.c").read_text())
    provenance = {
        "inputs": {str(path.relative_to(repo)): hashlib.sha256(path.read_bytes()).hexdigest() for path in (backend, header, playback, aout_header)},
        "extracted_functions": names,
        "method": "Exact source extraction, real pthread mutex/condition/join, mocked JNI and core policy",
        "scope": "Quiet-idle close cancellation only; no Android, device, sound, network, or blocked-JNI cancellation claim",
    }
    (out / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    variants = [("normal", [])]
    if args.sanitizers:
        variants.append(("sanitizers", ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]))
    results = {}
    for name, flags in variants:
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Wno-unused-parameter", "-pthread",
            *flags, str(out / "probe.c"), "-o", str(out / ("probe-" + name)),
        ]
        build = subprocess.run(command, text=True, capture_output=True)
        (out / ("build-" + name + ".log")).write_text(build.stdout + build.stderr)
        if build.returncode:
            raise SystemExit(build.stderr)
        env = os.environ.copy()
        if name == "sanitizers":
            env["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1"
            env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        try:
            run = subprocess.run([str(out / ("probe-" + name))], text=True, capture_output=True, env=env, timeout=10)
        except subprocess.TimeoutExpired:
            (out / ("result-" + name + ".txt")).write_text("FAIL: host close test exceeded the deadlock-guard timeout\n")
            raise SystemExit("Host close test timed out; no close-path pass")
        (out / ("result-" + name + ".txt")).write_text(run.stdout + run.stderr)
        print(name + ": " + run.stdout, end="")
        results[name] = run.returncode
        if run.returncode:
            raise SystemExit(run.stderr or run.returncode)
    (out / "result.json").write_text(json.dumps(results, indent=2) + "\n")
    print("Outputs: " + str(out))


if __name__ == "__main__":
    main()

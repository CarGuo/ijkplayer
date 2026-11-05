#!/usr/bin/env python3
"""Verify frozen fixture bytes and record portable input hashes."""
import argparse
import hashlib
import json
import sys
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("receipt", type=Path)
    parser.add_argument("ffmpeg_source", type=Path)
    parser.add_argument("ffmpeg_build", type=Path)
    args = parser.parse_args()
    test = Path(__file__).resolve().parent
    root = test.parents[2]
    # The checkpoint packet-record fixture is little endian. Fail clearly rather
    # than interpreting its size/timestamp fields incorrectly on another host.
    if sys.byteorder != "little":
        raise SystemExit("ordinary_video_packets.bin currently requires a little-endian host")
    expected = json.loads((test / "fixtures/manifest.json").read_text())["files"]
    for name, info in expected.items():
        path = test / "fixtures" / name
        if path.stat().st_size != info["bytes"] or sha(path) != info["sha256"]:
            raise SystemExit("Fixture integrity mismatch: " + name)
    files = [p for p in test.rglob("*") if p.is_file() and "__pycache__" not in p.parts]
    files += list((root / "ijkmedia/ijkplayer/android/pipeline").glob("*.h"))
    files += [root / name for name in [
        "ijkmedia/ijkplayer/android/pipeline/ffpipenode_android_mediacodec_vdec.c",
        "ijkmedia/ijkplayer/ff_ffplay_def.h",
        "ijkmedia/ijkplayer/ff_ffplay_options.h",
        "ijkmedia/ijksdl/android/ijksdl_codec_android_mediacodec_java.c",
        "ijkmedia/ijksdl/android/ijksdl_codec_android_mediacodec_java.h",
        "ijkmedia/ijksdl/android/ijksdl_codec_android_mediaformat_java.c",
        "ijkmedia/ijksdl/android/ijksdl_codec_android_mediaformat_java.h",
    ]]
    result = {"repository_files": {str(p.relative_to(root)): sha(p) for p in sorted(set(files))}}
    result["ffmpeg_build"] = {name: sha(args.ffmpeg_build / name) for name in [
        "config.h", "libavcodec/libavcodec.a", "libavutil/libavutil.a"]}
    result["ffmpeg_source_headers"] = {
        str(p.relative_to(args.ffmpeg_source)): sha(p)
        for subdir in ["libavcodec", "libavformat", "libavutil"]
        for p in sorted((args.ffmpeg_source / subdir).glob("*.h"))}
    args.receipt.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()

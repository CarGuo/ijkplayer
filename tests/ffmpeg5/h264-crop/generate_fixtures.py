#!/usr/bin/env python3
"""Generate the host corpus from local lavfi patterns, without external media.

Usage: python3 generate_fixtures.py /path/to/new-output-directory
The output must not already exist. FFmpeg and ffprobe default to PATH; override
them with FFMPEG and FFPROBE. Encoding bytes can differ across tool versions.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess


def sha(data):
    return hashlib.sha256(data).hexdigest()


def annex_nals(data):
    starts = list(re.finditer(b"\x00\x00(?:\x00)?\x01", data))
    assert starts and starts[0].start() == 0
    nals = [data[m.end():starts[i + 1].start() if i + 1 < len(starts) else len(data)]
            for i, m in enumerate(starts)]
    assert all(nals)
    return nals


def avcc_nals(data):
    nals = []
    offset = 0
    while offset < len(data):
        size = struct.unpack_from(">I", data, offset)[0]
        offset += 4
        assert size and offset + size <= len(data)
        nals.append(data[offset:offset + size])
        offset += size
    assert offset == len(data)
    return nals


def annex(nals):
    return b"".join(b"\0\0\0\1" + n for n in nals)


def avcc(nals):
    return b"".join(struct.pack(">I", len(n)) + n for n in nals)


def dump_bytes(text):
    # ffprobe -show_data uses a hexadecimal column, two spaces, then ASCII.
    return bytes.fromhex("".join(line.split(":", 1)[1].split("  ", 1)[0].strip()
                                 for line in text.splitlines() if ":" in line))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    work = output / "work"
    fixture = output / "fixtures"
    work.mkdir()
    fixture.mkdir()
    ffmpeg = os.environ.get("FFMPEG", "ffmpeg")
    ffprobe = os.environ.get("FFPROBE", "ffprobe")
    commands = []

    def run(argv):
        # Persist portable command tokens, never an invocation's absolute paths.
        commands.append([str(a).replace(str(output), "${OUTPUT}") for a in argv])
        result = subprocess.run([str(a) for a in argv], check=True, capture_output=True)
        return result.stdout

    versions = {name: run([tool, "-version"]).decode().splitlines()[:2]
                for name, tool in [("ffmpeg", ffmpeg), ("ffprobe", ffprobe)]}

    def encode(path, pattern, count, bframes, container):
        args = [ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi",
                "-i", pattern, "-frames:v", str(count), "-an", "-c:v", "libx264",
                "-pix_fmt", "yuv420p", "-threads", "1", "-preset", "medium", "-crf", "18",
                "-x264-params", "keyint=96:min-keyint=96:scenecut=0:open-gop=0:"
                + "bframes=" + str(bframes) + ":slices=1:repeat-headers=1"]
        if container == "mp4":
            args += ["-video_track_timescale", "90000"]
        run(args + ["-f", container, path])

    def transform(source, target, bsf):
        run([ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-f", "h264",
             "-i", source, "-map", "0:v:0", "-c:v", "copy", "-bsf:v", bsf,
             "-f", "h264", target])

    def probe(path, *fields):
        return json.loads(run([ffprobe, "-v", "error", "-select_streams", "v:0",
                               *fields, "-of", "json", path]))

    def save(name, data):
        path = fixture / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    # A is a real single-slice High-profile 640x360 picture (coded 640x368).
    # Only SPS cropping is changed for B: 368 - 96 = 272 visible lines.
    encode(work / "a.h264", "testsrc2=size=640x360:rate=24", 1, 2, "h264")
    transform(work / "a.h264", work / "b.h264", "h264_metadata=crop_bottom=96")
    a = annex_nals((work / "a.h264").read_bytes())
    b = annex_nals((work / "b.h264").read_bytes())
    assert [n for n in a if n[0] & 31 != 7] == [n for n in b if n[0] & 31 != 7]
    assert sum(n[0] & 31 == 5 for n in a) == 1
    save("actual_a.h264", annex(a))
    save("actual_b.h264", annex(b))
    save("avc3/first_A.avcc-au", avcc([n for n in a if n[0] & 31 not in (7, 8)]))
    save("avc3/boundary_B.avcc-au", avcc(b))

    # Removing only encoder SEI makes all NALs fit the 1-byte length-prefix test.
    encode(work / "small-full.h264", "color=black:size=32x32:rate=24", 1, 0, "h264")
    transform(work / "small-full.h264", work / "small-a.h264", "filter_units=remove_types=6")
    transform(work / "small-full.h264", work / "small-b.h264",
              "h264_metadata=crop_bottom=8,filter_units=remove_types=6")
    for suffix in ("a", "b"):
        units = annex_nals((work / ("small-" + suffix + ".h264")).read_bytes())
        assert all(len(n) < 256 for n in units)
        data = annex(units)
        assert len(data) < 200
        save("small_" + suffix + ".h264", data)

    # MP4 supplies actual reordered timestamps. Store every packet in Annex B,
    # not a made-up PTS sequence. repeat-headers preserves a source SPS/PPS pair.
    encode(work / "ordinary.mp4", "testsrc2=size=640x360:rate=24", 288, 2, "mp4")
    ordinary = probe(work / "ordinary.mp4", "-show_packets", "-show_streams")
    assert ordinary["streams"][0]["time_base"] == "1/90000"
    packets = ordinary["packets"]
    assert len(packets) == 288
    media = (work / "ordinary.mp4").read_bytes()
    records = []
    packet_rows = []
    for index, packet in enumerate(packets):
        start, size = int(packet["pos"]), int(packet["size"])
        units = avcc_nals(media[start:start + size])
        if index in (0, 96, 192):
            assert {5, 7, 8}.issubset({n[0] & 31 for n in units})
        data = annex(units)
        times = [int(packet[k]) for k in ("pts", "dts", "duration")]
        records.append(struct.pack("<Iqqq", len(data), *times) + data)
        packet_rows.append({"index": index, "bytes": len(data), "sha256": sha(data),
                            "pts": times[0], "dts": times[1], "duration": times[2]})
    assert any(p["pts"] != p["dts"] for p in packet_rows)
    save("ordinary_video_packets.bin", b"".join(records))

    # C is also newly generated, so the complete published corpus has one origin.
    encode(work / "c.mp4", "testsrc2=size=480x272:rate=24", 72, 2, "mp4")
    c = probe(work / "c.mp4", "-show_packets", "-show_streams", "-show_data")
    desc = dump_bytes(c["streams"][0]["extradata"])
    assert desc[0] == 1 and (desc[4] & 3) == 3
    first = c["packets"][0]
    data = (work / "c.mp4").read_bytes()
    start, size = int(first["pos"]), int(first["size"])
    units = [n for n in avcc_nals(data[start:start + size]) if n[0] & 31 not in (7, 8)]
    assert 5 in {n[0] & 31 for n in units}
    save("resolution/C_480x272.avcc", desc)
    save("resolution/first_C_480x272.avcc-au", avcc(units))

    dimensions = {}
    for name, path, expected in [
        ("A", work / "a.h264", (640, 360)),
        ("B", work / "b.h264", (640, 272)),
        ("small_A", work / "small-a.h264", (32, 32)),
        ("small_B", work / "small-b.h264", (32, 24)),
        ("C", work / "c.mp4", (480, 272)),
    ]:
        stream = probe(path, "-show_streams")["streams"][0]
        actual = (int(stream["width"]), int(stream["height"]))
        assert actual == expected, (name, actual, expected)
        dimensions[name] = list(actual)
        # Decode controls verify these are real pictures, not just parser inputs.
        run([ffmpeg, "-hide_banner", "-loglevel", "error", "-i", path,
             "-map", "0:v:0", "-f", "null", "-"])

    manifest = {
        "description": "New synthetic lavfi regression corpus, generated entirely by generate_fixtures.py. No external media or historical fixture bytes are inputs.",
        "generator": "generate_fixtures.py",
        "generator_sha256": sha(Path(__file__).read_bytes()),
        "versions": versions,
        "dimensions": dimensions,
        "ordinary_packet_count": 288,
        "files": {str(p.relative_to(fixture)): {"bytes": p.stat().st_size, "sha256": sha(p.read_bytes())}
                  for p in sorted(fixture.rglob("*")) if p.is_file()},
        "limits": "System FFmpeg/libx264 creates and software-checks media. The separate host runner exercises real FFmpeg 5.1.10 helpers. These generated media are not the historical real-device corpus.",
    }
    (fixture / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (fixture / "generation.json").write_text(json.dumps({"versions": versions,
        "commands": commands, "dimensions": dimensions}, indent=2) + "\n")
    (output / "generation.json").write_text(json.dumps({"versions": versions, "commands": commands,
        "dimensions": dimensions, "ordinary_packets": packet_rows}, indent=2) + "\n")
    print("Generated nine auditable fixture inputs:", fixture)
    print("Commands, timestamps, and hashes:", output / "generation.json")
    print("Playable generation intermediates (not historical device fixtures):", work)


if __name__ == "__main__":
    main()

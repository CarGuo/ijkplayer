#!/usr/bin/env python3
"""Static ELF checks for newly built Android libraries; never a runtime pass."""
import argparse
import hashlib
import json
import re
import subprocess
import struct
from pathlib import Path


def run(*args):
    return subprocess.check_output(args, text=True, errors="replace")


def symbol_sets(path):
    defined, undefined = set(), set()
    for line in run("readelf", "--dyn-syms", "-W", str(path)).splitlines():
        parts = line.split()
        if len(parts) < 8 or not parts[0].rstrip(":").isdigit(): continue
        name = parts[7].split("@")[0]
        (undefined if parts[6] == "UND" else defined).add(name)
    return defined, undefined


def relro_layout(program):
    loads, relros = [], []
    for line in program.splitlines():
        parts = line.split()
        if not parts or parts[0] not in ("LOAD", "GNU_RELRO"): continue
        start, size = int(parts[2], 16), int(parts[5], 16)
        (loads if parts[0] == "LOAD" else relros).append((start, start + size))
    result = {"segments": [{"start": hex(a), "end": hex(b), "end_aligned_16k": b % 16384 == 0} for a, b in relros]}
    for page in (4096, 16384):
        ranges = sorted((a // page * page, (b + page - 1) // page * page) for a, b in loads)
        covered = bool(relros)
        for start, end in relros:
            cursor, target = start // page * page, (end + page - 1) // page * page
            for a, b in ranges:
                if a <= cursor < b: cursor = max(cursor, b)
            if cursor < target: covered = False
        result[f"rounded_relro_dense_load_coverage_at_{page}"] = covered
        # Android reserves the complete image span before mapping individual LOADs;
        # inter-segment gaps are permitted, but RELRO must not extend past that span.
        reserved_start = min((a for a, _ in ranges), default=0)
        reserved_end = max((b for _, b in ranges), default=0)
        result[f"reserved_image_range_at_{page}"] = [hex(reserved_start), hex(reserved_end)]
        result[f"rounded_relro_within_reserved_image_at_{page}"] = bool(relros) and all(
            start // page * page >= reserved_start and (end + page - 1) // page * page <= reserved_end
            for start, end in relros)
    return result


# Deliberately narrow packaging checks; these do not evaluate runtime behavior.
LIBRARIES = ("libijkffmpeg.so", "libijksdl.so", "libijkplayer.so")
SYSTEM_NEEDED = {"libc.so", "libm.so", "libdl.so", "liblog.so", "libandroid.so",
                 "libz.so", "libOpenSLES.so", "libEGL.so", "libGLESv2.so"}
ELF_ABIS = {"arm64-v8a": (2, 183), "armeabi-v7a": (1, 40), "x86_64": (2, 62)}


def elf_identity(data):
    if len(data) < 20 or data[:4] != b"\x7fELF" or data[5] != 1:
        return {"valid_little_endian_elf": False}
    return {"valid_little_endian_elf": True, "class": data[4],
            "machine": struct.unpack_from("<H", data, 18)[0],
            "type": struct.unpack_from("<H", data, 16)[0]}


def load_layout(program, page):
    segments = []
    for line in program.splitlines():
        parts = line.split()
        if not parts or parts[0] != "LOAD":
            continue
        offset, vaddr, filesz, memsz, align = (int(parts[i], 16) for i in (1, 2, 4, 5, -1))
        segments.append({"offset": offset, "vaddr": vaddr, "filesz": filesz,
                         "memsz": memsz, "alignment": align,
                         "power_of_two_alignment": align > 0 and align & (align - 1) == 0,
                         "congruent_at_alignment": align > 0 and offset % align == vaddr % align,
                         "congruent_at_required_page": offset % page == vaddr % page,
                         "file_size_within_memory_size": filesz <= memsz})
    return segments


def needed_valid(name, needed):
    required = {"libijkffmpeg.so"} if name == "libijksdl.so" else (
        {"libijkffmpeg.so", "libijksdl.so"} if name == "libijkplayer.so" else set())
    return required <= set(needed) and set(needed) <= SYSTEM_NEEDED | set(LIBRARIES) and name not in needed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ijkplayer", type=Path)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent / "results/native-output-inventory.json")
    parser.add_argument("--ffmpeg-version", default="n5.1.10")
    parser.add_argument("--openssl-version", default="OpenSSL 3.5.9 29 Sep 2026")
    args = parser.parse_args()
    records, checks = [], []
    for module, abi, alignment in [("arm64", "arm64-v8a", 16384), ("armv7a", "armeabi-v7a", 4096), ("x86_64", "x86_64", 16384)]:
        libs = args.ijkplayer / f"android/ijkplayer/ijkplayer-{module}/src/main/libs/{abi}"
        symbols = {}
        found = sorted(p.name for p in libs.glob("*.so"))
        checks.append({"test": f"{abi}:exact_library_set", "passed": found == sorted(LIBRARIES), "found": found})
        for name in LIBRARIES:
            path = libs / name
            if not path.exists():
                checks.append({"test": f"{abi}/{name}:exists", "passed": False}); continue
            data = path.read_bytes()
            identity = elf_identity(data)
            expected_class, expected_machine = ELF_ABIS[abi]
            identity_ok = identity.get("class") == expected_class and identity.get("machine") == expected_machine and identity.get("type") == 3
            checks.append({"test": f"{abi}/{name}:ELF_identity", "passed": identity_ok, "observed": identity})
            if not identity_ok:
                continue
            program, dynamic = run("readelf", "-lW", str(path)), run("readelf", "-dW", str(path))
            strings = run("strings", str(path)).splitlines()
            defined, undefined = symbol_sets(path)
            symbols[name] = (defined, undefined)
            aligns = sorted(set(int(line.split()[-1], 16) for line in program.splitlines() if line.strip().startswith("LOAD")))
            needed = re.findall(r"\(NEEDED\).*?\[(.*?)\]", dynamic)
            record = {"abi": abi, "library": name, "path": str(path), "bytes": len(data),
                      "sha256": hashlib.sha256(data).hexdigest(), "load_alignments": [hex(n) for n in aligns],
                      "needed": needed, "soname": re.findall(r"\(SONAME\).*?\[(.*?)\]", dynamic),
                      "version_strings": [s for s in strings if re.fullmatch(r"(?:n[0-9]\..*|ff[0-9].*|ijk-n.*|OpenSSL [0-9].*)", s)],
                      "undefined_dynamic_symbols": sorted(undefined), "exported_dynamic_symbol_count": len(defined),
                      "mediacodec_auto_fallback_option": "mediacodec-auto-fallback" in strings,
                      "ffmpeg_configuration": [s for s in strings if s.startswith("--") and "--enable-openssl" in s]}
            record["elf_identity"] = identity
            record["load_segments"] = load_layout(program, alignment)
            record["gnu_relro_layout"] = relro_layout(program)
            records.append(record)
            checks.append({"test": f"{abi}/{name}:LOAD_congruence_and_sizes", "passed": bool(record["load_segments"]) and all(
                s["power_of_two_alignment"] and s["congruent_at_alignment"] and s["congruent_at_required_page"] and s["file_size_within_memory_size"] for s in record["load_segments"])})
            checks.append({"test": f"{abi}/{name}:NEEDED_dependencies", "passed": needed_valid(name, needed), "observed": needed})
            checks.append({"test": f"{abi}/{name}:LOAD_alignment", "passed": bool(aligns) and all(n >= alignment for n in aligns), "minimum": hex(alignment)})
            checks.append({"test": f"{abi}/{name}:SONAME", "passed": record["soname"] == [name]})
            relro = record["gnu_relro_layout"]
            checks.append({"test": f"{abi}/{name}:RELRO_within_image_4KB", "passed": relro["rounded_relro_within_reserved_image_at_4096"]})
            if alignment == 16384:
                checks.append({"test": f"{abi}/{name}:RELRO_within_image_16KB", "passed": relro["rounded_relro_within_reserved_image_at_16384"]})
                checks.append({"test": f"{abi}/{name}:RELRO_end_aligned_16KB", "passed": bool(relro["segments"]) and all(s["end_aligned_16k"] for s in relro["segments"])})
            if name == "libijkffmpeg.so":
                checks.append({"test": f"{abi}:FFmpeg5_version", "passed": args.ffmpeg_version in strings})
            if name == "libijkffmpeg.so":
                checks.append({"test": f"{abi}:OpenSSL_identity", "passed": args.openssl_version in strings})
            if name == "libijkplayer.so":
                checks.append({"test": f"{abi}:smart_fallback_option", "passed": record["mediacodec_auto_fallback_option"]})
        if "libijkffmpeg.so" in symbols:
            exports = symbols["libijkffmpeg.so"][0]
            for client in ("libijkplayer.so", "libijksdl.so"):
                if client not in symbols: continue
                required = sorted(s for s in symbols[client][1] if re.match(r"(?:av\w*_|ff\w*_|swr_|sws_)", s))
                missing = sorted(set(required) - exports)
                checks.append({"test": f"{abi}/{client}:FFmpeg_symbol_resolution", "passed": bool(required) and not missing,
                               "required_symbols": required, "missing_symbols": missing})
    checks.append({"test": "complete_nine_library_inventory", "passed": len(records) == 9})
    report = {"expected_identity": {"ffmpeg": args.ffmpeg_version, "openssl": args.openssl_version},
              "scope": "Static Android ELF packaging, ABI, SONAME, dependencies and dynamic-symbol checks only; no runtime, broad security audit, vendor/device or decoder certification. ARMv7 uses a 4KB baseline; minimum API comes from build evidence, not ELF inspection.",
              "libraries": records, "checks": checks, "passed": all(c["passed"] for c in checks)}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    for check in checks: print(("PASS " if check["passed"] else "FAIL ") + check["test"])
    print(args.output)
    raise SystemExit(0 if report["passed"] else 1)


if __name__ == "__main__": main()

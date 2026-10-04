#!/usr/bin/env python3
"""Offline positive/negative unit fixtures for narrow ELF packaging inspection."""
import struct
import unittest
from pathlib import Path
import check_native_outputs as checker

PROGRAM = (Path(__file__).parent / "native-fixtures/readelf-program-16k.txt").read_text()

class NativeOutputTests(unittest.TestCase):
    def test_valid_elf_classes_and_machines(self):
        for cls, machine in checker.ELF_ABIS.values():
            data = bytearray(64)
            data[:6] = b"\x7fELF" + bytes([cls, 1])
            struct.pack_into("<HH", data, 16, 3, machine)
            self.assertEqual(checker.elf_identity(data), {
                "valid_little_endian_elf": True, "class": cls, "machine": machine, "type": 3})

    def test_invalid_and_truncated_elf(self):
        for data in (b"", b"not an ELF" * 10, b"\x7fELF\x02\x02" + b"\0" * 64):
            self.assertFalse(checker.elf_identity(data)["valid_little_endian_elf"])

    def test_load_layout_valid(self):
        loads = checker.load_layout(PROGRAM, 16384)
        self.assertEqual(len(loads), 2)
        self.assertTrue(all(s["congruent_at_alignment"] and s["congruent_at_required_page"] for s in loads))

    def test_bad_load_offset(self):
        loads = checker.load_layout(PROGRAM.replace("0x004000 0x0000000000008000", "0x005000 0x0000000000008000"), 16384)
        self.assertFalse(loads[1]["congruent_at_alignment"])
        self.assertFalse(loads[1]["congruent_at_required_page"])

    def test_bad_load_sizes_and_alignment(self):
        loads = checker.load_layout(PROGRAM.replace("0x004000 0x004000 RW  0x4000", "0x005000 0x004000 RW  0x3000"), 16384)
        self.assertFalse(loads[1]["power_of_two_alignment"])
        self.assertFalse(loads[1]["file_size_within_memory_size"])

    def test_relro_good_end_and_range(self):
        result = checker.relro_layout(PROGRAM)
        self.assertTrue(result["rounded_relro_within_reserved_image_at_16384"])
        self.assertTrue(result["segments"][0]["end_aligned_16k"])

    def test_relro_bad_end(self):
        result = checker.relro_layout(PROGRAM.replace("0x002000 0x004000 R", "0x002000 0x003000 R"))
        self.assertFalse(result["segments"][0]["end_aligned_16k"])

    def test_relro_outside_image(self):
        result = checker.relro_layout(PROGRAM.replace("0x002000 0x004000 R", "0x002000 0x008000 R"))
        self.assertFalse(result["rounded_relro_within_reserved_image_at_16384"])

    def test_missing_relro(self):
        result = checker.relro_layout("\n".join(l for l in PROGRAM.splitlines() if "GNU_RELRO" not in l))
        self.assertFalse(result["rounded_relro_within_reserved_image_at_16384"])

    def test_armv7_4k_baseline(self):
        result = checker.load_layout(PROGRAM.replace("0x4000", "0x1000"), 4096)
        self.assertTrue(all(s["congruent_at_required_page"] for s in result))
        self.assertTrue(all(s["alignment"] == 4096 for s in result))

    def test_needed_positive_and_negative(self):
        self.assertTrue(checker.needed_valid("libijkplayer.so", ["libijkffmpeg.so", "libijksdl.so", "libc.so"]))
        self.assertTrue(checker.needed_valid("libijksdl.so", ["libijkffmpeg.so", "libOpenSLES.so"]))
        self.assertTrue(checker.needed_valid("libijkffmpeg.so", ["libc.so", "libz.so"]))
        self.assertFalse(checker.needed_valid("libijkplayer.so", ["libijkffmpeg.so"]))
        self.assertFalse(checker.needed_valid("libijkffmpeg.so", ["libssl.so.3"]))
        self.assertFalse(checker.needed_valid("libijkffmpeg.so", ["libijkffmpeg.so"]))

if __name__ == "__main__":
    unittest.main(verbosity=2)

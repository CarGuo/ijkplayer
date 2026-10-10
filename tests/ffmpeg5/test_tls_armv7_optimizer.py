#!/usr/bin/env python3
"""Test the real TLS flag-selection block without building native libraries."""
import hashlib
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BUILDER = ROOT / "android/contrib/compile-ffmpeg5.sh"
SOURCE = BUILDER.read_text()
FLAGS_START = SOURCE.index('SSL_FLAGS=("$SSL"')
IDENTITY_START = SOURCE.index("SSL_IDENTITY=$(", FLAGS_START)
FLAG_SELECTION = SOURCE[FLAGS_START:IDENTITY_START]
IDENTITY_FLAGS = "printf 'configure_flags='; printf '%q ' \"${SSL_FLAGS[@]}\"; printf '\\n'"

ANDROID_11 = "Android (7155654, based on r399163b) clang version 11.0.5 (https://android.googlesource.com/toolchain/llvm-project 8f7e8d9)"
ANDROID_18 = "Android (11951724, +pgo, +bolt, based on r522817) clang version 18.0.1"
APPLE_11 = "Apple clang version 11.0.3 (clang-1103.0.32.62)"


class TlsArmv7OptimizerTests(unittest.TestCase):
    def select(self, arch, major, banner=ANDROID_11):
        with tempfile.TemporaryDirectory(prefix="ijk-tls-flags-") as tmp:
            tmp = Path(tmp)
            compiler = tmp / "mock-clang"
            calls = tmp / "compiler-calls"
            compiler.write_text("""#!/usr/bin/env bash
set -eu
printf '%s\\n' "$*" >> "$MOCK_CALLS"
if [[ "$*" == '--version' ]]; then
 printf '%s\\n' "$MOCK_BANNER"
elif [[ "$*" == '-dM -E -x c /dev/null' ]]; then
 printf '%s\\n' '#define __ANDROID__ 1' '#define __ANDROID_API__ 16'
 if [[ -n "$MOCK_MAJOR" ]]; then
  printf '#define __clang__ 1\\n#define __clang_major__ %s\\n' "$MOCK_MAJOR"
 fi
else
 exit 9
fi
""")
            compiler.chmod(0o755)
            env = dict(os.environ, ARCH=arch, API="16" if arch == "armv7a" else "21",
                       SSL="android-arm" if arch == "armv7a" else "android-" + arch,
                       CC=str(compiler), MOCK_MAJOR=str(major), MOCK_BANNER=banner,
                       MOCK_CALLS=str(calls))
            result = subprocess.run(["bash", "-euo", "pipefail", "-c",
                                     FLAG_SELECTION + "\n" + IDENTITY_FLAGS],
                                    env=env, text=True, capture_output=True, check=True)
            call_lines = calls.read_text().splitlines() if calls.exists() else []
            return result.stdout, call_lines

    def test_android_r22b_clang11_armv7_enabled(self):
        flags, calls = self.select("armv7a", 11, ANDROID_11)
        self.assertEqual(flags.count("-fno-unroll-loops"), 1)
        self.assertIn("-D__ANDROID_API__=16", flags)
        self.assertEqual(calls, ["-dM -E -x c /dev/null"])

    def test_banner_format_does_not_control_guard(self):
        for banner in (ANDROID_11, APPLE_11, "clang version 11.0.0", "vendor-custom compiler"):
            with self.subTest(banner=banner):
                flags, _ = self.select("armv7a", 11, banner)
                self.assertIn("-fno-unroll-loops", flags)

    def test_other_clang_majors_disabled(self):
        for major in (10, 12, 17, 18, 21):
            with self.subTest(major=major):
                flags, calls = self.select("armv7a", major, ANDROID_18)
                self.assertNotIn("-fno-unroll-loops", flags)
                self.assertEqual(calls, ["-dM -E -x c /dev/null"])

    def test_non_clang_disabled(self):
        flags, _ = self.select("armv7a", "", "gcc version 11.5.0")
        self.assertNotIn("-fno-unroll-loops", flags)

    def test_other_abis_unchanged_and_no_probe(self):
        for arch in ("arm64", "x86_64"):
            with self.subTest(arch=arch):
                flags, calls = self.select(arch, 11)
                self.assertNotIn("-fno-unroll-loops", flags)
                self.assertIn("-D__ANDROID_API__=21", flags)
                self.assertEqual(calls, [])

    def test_flags_are_bound_into_existing_cache_identity(self):
        identity_end = SOURCE.index("SSL_CACHE_KEY=", IDENTITY_START)
        self.assertIn(IDENTITY_FLAGS, SOURCE[IDENTITY_START:identity_end])
        self.assertIn('SSL_CACHE_KEY=$(printf', SOURCE[identity_end:identity_end + 120])
        changed, _ = self.select("armv7a", 11)
        unchanged, _ = self.select("armv7a", 12)
        self.assertNotEqual(hashlib.sha256(changed.encode()).hexdigest(),
                            hashlib.sha256(unchanged.encode()).hexdigest())
        self.assertEqual(changed.replace(" -fno-unroll-loops", ""), unchanged)
        self.assertIn('"${SSL_FLAGS[@]}"', SOURCE[SOURCE.index('ssl_run perl'):])


if __name__ == "__main__":
    unittest.main()

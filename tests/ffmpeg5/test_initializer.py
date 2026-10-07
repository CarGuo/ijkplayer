#!/usr/bin/env python3
"""Offline initializer integration test using real delivered repositories.

Usage: python3 tests/ffmpeg5/test_initializer.py /absolute/path/to/FFmpeg
The current IJK tree must contain the three pinned dependency checkouts.
Only a temporary copy is changed; dependencies are reused through read-only
initializer checks. No remote fetch is permitted by the test environment.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PIN = 'c157591220febd96fdcffb05283f63dd38627081'
DEPS = {
    'extra/openssl-3.5.9': '45e844fa2a14ec92d146bd8f5778ac130b6625fb',
    'ijkmedia/ijkyuv': 'a0c7dd3e4b095649881f39152655437b8cde1589',
    'ijkmedia/ijksoundtouch': '6bf39cd3bf6b0c156267d12446b0d6bdcfcd53c2',
}

def git(path, *args):
    return subprocess.check_output(['git', '-C', str(path), *args], text=True).strip()

def snapshot(path):
    return git(path, 'rev-parse', 'HEAD'), git(path, 'status', '--porcelain', '--untracked-files=all')

def main():
    source = Path(sys.argv[1]).resolve()
    assert git(source, 'rev-parse', 'HEAD') == PIN
    originals = {str(source): snapshot(source)}
    for dep, pin in DEPS.items():
        assert git(ROOT / dep, 'rev-parse', 'HEAD') == pin, dep
        originals[str(ROOT / dep)] = snapshot(ROOT / dep)
    original_ffmpeg = ROOT / 'extra/ffmpeg5'
    if original_ffmpeg.exists():
        originals[str(original_ffmpeg)] = snapshot(original_ffmpeg)
    with tempfile.TemporaryDirectory(prefix='ijk-initializer-') as tmp:
        work = Path(tmp) / 'test tree with spaces'
        work.mkdir()
        shutil.copy2(ROOT / 'init-android-ffmpeg5.sh', work)
        shutil.copytree(ROOT / 'patches/ffmpeg5', work / 'patches/ffmpeg5')
        for dep in DEPS:
            target = work / dep
            target.parent.mkdir(parents=True, exist_ok=True)
            target.symlink_to(ROOT / dep, target_is_directory=True)
        env = dict(os.environ, FFMPEG5_SOURCE_REPOSITORY=str(source), GIT_ALLOW_PROTOCOL='file')
        def run(label, success=True, expected=''):
            result = subprocess.run(['bash', './init-android-ffmpeg5.sh'], cwd=work,
                                    env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            assert (result.returncode == 0) == success, (label, result.stdout)
            assert expected in result.stdout, (label, result.stdout)
            if success:
                assert result.stdout.count('is already applied') == 6, result.stdout
            print('PASS:', label, flush=True)
        run('fresh local repository initialization')
        checkout = work / 'extra/ffmpeg5'
        assert git(checkout, 'rev-parse', 'HEAD') == PIN
        assert not git(checkout, 'status', '--porcelain')
        before = snapshot(checkout)
        run('idempotent pinned initialization')
        assert snapshot(checkout) == before
        patch = work / 'patches/ffmpeg5/0005-mov-hevc-seek-replay.patch'
        git(checkout, 'apply', '--reverse', str(patch))
        before = snapshot(checkout)
        contents = (checkout / 'libavformat/mov.c').read_bytes()
        run('missing fifth patch rejected', False, 'Expected patch 0005')
        assert snapshot(checkout) == before
        assert (checkout / 'libavformat/mov.c').read_bytes() == contents
        git(checkout, 'apply', str(patch))
        patch = work / 'patches/ffmpeg5/0006-rtsp-initial-open.patch'
        git(checkout, 'apply', '--reverse', str(patch))
        before = snapshot(checkout)
        rtsp_files = ('libavformat/rtsp.c', 'libavformat/rtsp.h', 'libavformat/rtspdec.c')
        contents = {name: (checkout / name).read_bytes() for name in rtsp_files}
        run('missing sixth patch rejected', False, 'Expected patch 0006')
        assert snapshot(checkout) == before
        assert {name: (checkout / name).read_bytes() for name in rtsp_files} == contents
        git(checkout, 'apply', str(patch))
        git(checkout, 'checkout', '--detach', PIN + '^')
        before = snapshot(checkout)
        run('wrong revision preserved', False, 'Unexpected source revision')
        assert snapshot(checkout) == before
        checkout.rename(work / 'saved-checkout')
        checkout.mkdir()
        (checkout / 'sentinel').write_text('preserve me')
        run('non-repository directory preserved', False, 'Refusing to overwrite')
        assert (checkout / 'sentinel').read_text() == 'preserve me'
        shutil.rmtree(checkout)
        env['FFMPEG5_SOURCE_REPOSITORY'] = str(work / 'missing')
        run('invalid local override rejected', False, 'existing local repository or bundle')
        assert not checkout.exists()
        # A full bundle is independently cloneable without the source repo.
        bundle = work / 'recovery.bundle'
        git(source, 'bundle', 'create', str(bundle), '--all')
        env['FFMPEG5_SOURCE_REPOSITORY'] = str(bundle)
        run('fresh standalone bundle initialization')
        assert git(checkout, 'rev-parse', 'HEAD') == PIN
    for path, before in originals.items():
        assert snapshot(Path(path)) == before, 'Original checkout changed: ' + path
    print('PASS: original source and dependency checkouts unchanged', flush=True)

if __name__ == '__main__':
    main()

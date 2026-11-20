#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Replace the fork's OpenVR runtime in a verified base EROFS layer, without touching a user disk."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as source:
        for data in iter(lambda: source.read(1024 * 1024), b''):
            h.update(data)
    return h.hexdigest()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base', type=Path, required=True)
    p.add_argument('--runtime', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    header = a.runtime.read_bytes()[:64]
    if header[:4] != b'\x7fELF' or header[4] != 2 or struct.unpack_from('<H', header, 18)[0] != 62:
        p.error('runtime must be an x86-64 ELF shared library')
    repo = Path(__file__).resolve().parents[2]
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip()
    epoch = subprocess.check_output(['git', 'show', '-s', '--format=%ct', 'HEAD'], cwd=repo, text=True).strip()
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='satchel-layer-', dir=a.output.parent) as directory:
        directory = Path(directory)
        root, image = directory / 'root', directory / 'layer.img'
        subprocess.run(['fsck.erofs', '--extract=' + str(root), '--no-preserve-owner', str(a.base.resolve())], check=True)
        target = root / 'usr/lib/steamac/vr/runtime/bin/linux64/vrclient.so'
        if not target.is_file():
            raise RuntimeError('base layer does not contain the expected VR runtime')
        shutil.copyfile(a.runtime, target)
        target.chmod(0o755)
        release = root / 'usr/lib/steamac/satchel-release'
        release.write_text('Satchel tracking fork source=' + commit + '\nruntime-sha256=' + sha(target) + '\n')
        release.chmod(0o644)
        layer_release = root / 'usr/lib/steamac/layer-release'
        digest = hashlib.sha256()
        for f in sorted(root.rglob('*'), key=lambda f: str(f.relative_to(root)).encode()):
            if f.is_file() and not f.is_symlink() and f != layer_release:
                digest.update((sha(f) + '  ' + str(f.relative_to(root)) + '\n').encode())
        text = layer_release.read_text()
        text = re.sub(r'content=\S+', 'content=' + digest.hexdigest()[:16], text)
        layer_release.write_text(text.rstrip() + ' satchel=' + commit[:12] + '\n')
        subprocess.run(['mkfs.erofs', '-b', '4096', '-zlz4hc', '-T', epoch, '-U',
                        '5fea1f2a-0c6b-4d5e-9a1e-0000000000a1', '--all-root', '-L',
                        'steamac-layer', str(image), str(root)], check=True, stdout=subprocess.DEVNULL)
        subprocess.run(['fsck.erofs', str(image)], check=True)
        os.replace(image, a.output)
    manifest = {'kind': 'satchel-vr-layer', 'source_commit': commit, 'base_sha256': sha(a.base),
                'runtime_sha256': sha(a.runtime), 'layer_sha256': sha(a.output),
                'modified': ['usr/lib/steamac/vr/runtime/bin/linux64/vrclient.so',
                             'usr/lib/steamac/satchel-release', 'usr/lib/steamac/layer-release']}
    a.output.with_suffix('.manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest))

if __name__ == '__main__':
    main()

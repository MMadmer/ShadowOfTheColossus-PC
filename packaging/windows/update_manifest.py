# SPDX-License-Identifier: GPL-2.0-or-later
"""sotc: writes update-manifest.txt into a packaged port folder (package_sotc.sh).

The launcher's updater (launcher/sotc_update.py) reads it out of the release zip by HTTP range
and downloads only the files whose SHA-256 differs from the installed ones:

    schema=sotc-pc.update/1
    version=1.1.1
    <sha256 hex>\t<size>\t<path relative to SotC-PC, '/' separated>

    python update_manifest.py <SotC-PC folder> <version>
"""
import hashlib
from pathlib import Path
import sys

NAME = 'update-manifest.txt'


def main(root, version):
    root = Path(root)
    rows = []
    for path in sorted(p for p in root.rglob('*') if p.is_file()):
        rel = path.relative_to(root).as_posix()
        if rel == NAME:
            continue
        digest = hashlib.sha256()
        with open(path, 'rb') as f:
            for chunk in iter(lambda: f.read(1 << 20), b''):
                digest.update(chunk)
        rows.append(f'{digest.hexdigest()}\t{path.stat().st_size}\t{rel}')
    text = f'schema=sotc-pc.update/1\nversion={version}\n' + ''.join(row + '\n' for row in rows)
    (root / NAME).write_bytes(text.encode('utf-8'))
    print(f'{NAME}: {len(rows)} files, version {version}')


if __name__ == '__main__':
    main(*sys.argv[1:3])

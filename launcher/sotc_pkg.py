# SPDX-License-Identifier: GPL-2.0-or-later
"""sotc: the player's own PS4 packages (.pkg dumps: the game, its update, its DLC) unpacked into
the folders the port runs from, next to the packages:

    <folder>/CUSA08809          the game (eboot.bin, sce_sys/param.sfo, ...)
    <folder>/CUSA08809-UPDATE   the update (found next to the game, as shadPS4 does)
    <folder>/CUSA08809-DLC      one folder per add-on (sce_sys/param.sfo, its files if it has any):
                                the launcher's DLC folder (SOTC_DLC_DIR)

The package entries the port reads (param.sfo, playgo-chunk, icons, ...) are written by this
module; the file system image (PFS) is extracted by PkgTool (LibOrbisPkg, LGPL-3.0, shipped in
tools/PkgTool), which handles fake packages (dumps) only. Nothing is downloaded or decrypted with
console keys."""
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import tempfile
import threading
import time

GAME_ID = 'CUSA08809'
PKG_MAGIC = b'\x7fCNT'
CONTENT_TYPE_ADDON = 0x1b
ENTRY_ENCRYPTED = 0x80000000
# The package entries written into sce_sys (shadPS4 pkg_type.cpp); trophies below.
ENTRY_NAMES = {
    0x1000: 'param.sfo', 0x1001: 'playgo-chunk.dat', 0x1002: 'playgo-chunk.sha',
    0x1003: 'playgo-manifest.xml', 0x1004: 'pronunciation.xml', 0x1005: 'pronunciation.sig',
    0x1006: 'pic1.png', 0x1007: 'pubtoolinfo.dat', 0x1008: 'app/playgo-chunk.dat',
    0x1009: 'app/playgo-chunk.sha', 0x100A: 'app/playgo-manifest.xml', 0x100B: 'shareparam.json',
    0x100C: 'shareoverlayimage.png', 0x100D: 'save_data.png', 0x100E: 'shareprivacyguardimage.png',
    0x1200: 'icon0.png', 0x1220: 'pic0.png', 0x1240: 'snd0.at9', 0x1260: 'changeinfo/changeinfo.xml',
    0x1280: 'icon0.dds', 0x12A0: 'pic0.dds', 0x12C0: 'pic1.dds',
}


def entry_name(entry_id):
    if 0x1400 <= entry_id < 0x1480:
        return f'trophy/trophy{entry_id - 0x1400:02d}.trp'
    return ENTRY_NAMES.get(entry_id)


def read_package(path):
    """{'path', 'content_id', 'category', 'title', 'version', 'kind' (game/update/dlc), 'pfs_size',
    'entries'} of a package of the game, or None (not a package, another game)."""
    try:
        with open(path, 'rb') as f:
            header = f.read(0x430)
            if len(header) < 0x430 or header[:4] != PKG_MAGIC:
                return None
            count = struct.unpack_from('>I', header, 0x10)[0]
            table = struct.unpack_from('>I', header, 0x18)[0]
            if count > 4096:
                return None
            f.seek(table)
            raw = f.read(count * 32)
            entries = []
            for i in range(count):
                entry_id, _name, flags1, _flags2, offset, size = struct.unpack_from('>6I', raw, i * 32)
                entries.append((entry_id, flags1, offset, size))
            sfo_data = b''
            for entry_id, flags1, offset, size in entries:
                if entry_id == 0x1000 and not flags1 & ENTRY_ENCRYPTED:
                    f.seek(offset)
                    sfo_data = f.read(size)
    except (OSError, struct.error):
        return None
    content_id = header[0x40:0x64].split(b'\0')[0].decode('ascii', 'replace')
    content_type = struct.unpack_from('>I', header, 0x74)[0]
    try:
        from prepare import sfo
        values = sfo(sfo_data) if sfo_data else {}
    except (ImportError, ValueError):
        values = {}
    if GAME_ID not in content_id and values.get('TITLE_ID') != GAME_ID:
        return None
    category = str(values.get('CATEGORY', ''))
    if content_type == CONTENT_TYPE_ADDON or category.startswith('ac'):
        kind = 'dlc'
    elif category == 'gp':
        kind = 'update'
    else:
        kind = 'game'
    return {
        'path': str(path), 'content_id': content_id, 'category': category, 'kind': kind,
        'title': str(values.get('TITLE', '')).replace('�', '').strip(),
        'version': str(values.get('APP_VER') or values.get('VERSION') or ''),
        'pfs_size': struct.unpack_from('>Q', header, 0x418)[0], 'entries': entries,
    }


def find_packages(folder, depth=2):
    """The game's packages in folder and below (depth levels)."""
    found = []
    pending = [(Path(folder), 0)]
    while pending:
        path, level = pending.pop()
        try:
            entries = list(os.scandir(path))
        except OSError:
            continue
        for entry in entries:
            if entry.is_file() and entry.name.lower().endswith('.pkg'):
                info = read_package(entry.path)
                if info:
                    found.append(info)
            elif entry.is_dir(follow_symlinks=False) and level < depth and not entry.name.startswith('.'):
                pending.append((Path(entry.path), level + 1))
    return found


def targets(folder):
    """Where unpack() puts the game, its update and the DLC for packages in folder."""
    folder = Path(folder)
    return {'game': folder / GAME_ID, 'update': folder / f'{GAME_ID}-UPDATE', 'dlc': folder / f'{GAME_ID}-DLC'}


def _dlc_folder(info, root):
    label = info['content_id'][20:] if len(info['content_id']) > 20 else Path(info['path']).stem
    return Path(root) / ''.join(c if c.isalnum() or c in '-_' else '_' for c in label)


def _write_entries(info, dest):
    """The package's plain entries into dest/sce_sys (the PFS holds the rest)."""
    sce_sys = Path(dest) / 'sce_sys'
    with open(info['path'], 'rb') as f:
        for entry_id, flags1, offset, size in info['entries']:
            name = entry_name(entry_id)
            if not name or flags1 & ENTRY_ENCRYPTED:
                continue
            target = sce_sys / name
            target.parent.mkdir(parents=True, exist_ok=True)
            f.seek(offset)
            target.write_bytes(f.read(size))


def _unpacked(info, dest):
    """True when dest already holds an unpacked package of that kind (never overwritten: it may be
    the player's own copy)."""
    dest = Path(dest)
    if not (dest / 'sce_sys' / 'param.sfo').is_file():
        return False
    return info['kind'] != 'game' or (dest / 'eboot.bin').is_file()


def _tree_bytes(path):
    total = 0
    for root, _dirs, files in os.walk(path):
        for name in files:
            try:
                total += os.path.getsize(os.path.join(root, name))
            except OSError:
                pass
    return total


def _extract_pfs(info, dest, pkgtool, progress, done_before, total):
    """PkgTool's extraction of the package's file system into dest (merged with what is there)."""
    dest = Path(dest)
    dest.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='sotc_pkg_', dir=dest.parent))
    pkg = Path(info['path'])
    # PkgTool maps the package for writing: a read-only package (as copied from discs) is made
    # writable for the extraction only, and read-only again afterwards.
    read_only = not os.access(pkg, os.W_OK)
    try:
        if read_only:
            os.chmod(pkg, stat.S_IREAD | stat.S_IWRITE)
        process = subprocess.Popen([str(pkgtool), 'pkg_extract', str(pkg), str(work)],
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        output = []
        reader = threading.Thread(target=lambda: output.extend(process.stdout), daemon=True)
        reader.start()
        while process.poll() is None:
            if progress:
                progress(done_before + min(_tree_bytes(work), info['pfs_size']), total)
            time.sleep(1.0)
        reader.join(5)
        root = work / 'uroot'
        if process.returncode != 0 or not root.is_dir():
            text = b''.join(output).decode('utf-8', 'replace').strip().splitlines()
            raise RuntimeError(f'PkgTool failed on {pkg.name}: ' + (text[0] if text else f'code {process.returncode}'))
        for item in root.iterdir():
            target = dest / item.name
            if item.is_dir() and target.is_dir():
                shutil.copytree(item, target, dirs_exist_ok=True)
                shutil.rmtree(item)
            else:
                if target.is_dir():
                    shutil.rmtree(target)
                elif target.exists():
                    target.unlink()
                shutil.move(str(item), str(target))
    finally:
        if read_only:
            try:
                os.chmod(pkg, stat.S_IREAD)
            except OSError:
                pass
        shutil.rmtree(work, ignore_errors=True)


def pkgtool_path(port_dir):
    for candidate in (Path(port_dir) / 'tools' / 'PkgTool' / 'PkgTool.exe',
                      Path(port_dir).parent / 'tools' / 'PkgTool' / 'PkgTool.exe'):
        if candidate.is_file():
            return candidate
    return None


def needed_bytes(packages, folder):
    """Bytes the packages not unpacked yet will take in folder."""
    places = targets(folder)
    total = 0
    for info in packages:
        dest = _dlc_folder(info, places['dlc']) if info['kind'] == 'dlc' else places[info['kind']]
        if not _unpacked(info, dest):
            total += info['pfs_size']
    return total


def unpack(packages, folder, port_dir, progress=None):
    """Unpacks the packages (read_package records) into folder; returns targets(folder) with the
    paths that exist. progress(done_bytes, total_bytes) is called from this thread."""
    places = targets(folder)
    pkgtool = pkgtool_path(port_dir)
    # The newest update wins; every DLC is kept.
    games = [p for p in packages if p['kind'] == 'game']
    updates = sorted((p for p in packages if p['kind'] == 'update'), key=lambda p: p['version'])
    chosen = games[:1] + updates[-1:] + [p for p in packages if p['kind'] == 'dlc']
    todo = []
    for info in chosen:
        dest = _dlc_folder(info, places['dlc']) if info['kind'] == 'dlc' else places[info['kind']]
        if not _unpacked(info, dest):
            todo.append((info, dest))
    total = sum(info['pfs_size'] for info, _dest in todo) or 1
    free = shutil.disk_usage(folder).free
    if total > free:
        raise RuntimeError(f'Not enough disk space: {total >> 30} GB needed, {free >> 30} GB free')
    done = 0
    for info, dest in todo:
        if info['pfs_size']:
            if not pkgtool:
                raise RuntimeError('tools/PkgTool/PkgTool.exe is missing')
            _extract_pfs(info, dest, pkgtool, progress, done, total)
        _write_entries(info, dest)
        done += info['pfs_size']
        if progress:
            progress(done, total)
    return {kind: str(path) for kind, path in places.items() if path.exists()}

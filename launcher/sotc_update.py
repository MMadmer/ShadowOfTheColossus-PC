# SPDX-License-Identifier: GPL-2.0-or-later
"""sotc: launcher self-update from the port's GitHub releases.

The launcher asks GitHub for the latest release (SOTC_UPDATE_URL replaces the API address, for
tests: a file:// or http:// URL of the same JSON). When its tag is newer than the running
version and the user agrees, SotC-PC.zip is downloaded to <port>/update/, checked (size, the
SHA-256 digest GitHub reports for the asset, zip integrity, layout) and unpacked to
<port>/update/new/SotC-PC/.

Delta updates (as Dead Air: Refined does): the release zip carries update-manifest.txt (SHA-256 and
size of every file, packaging/windows/update_manifest.py). During the check the launcher reads the
zip's central directory and that manifest by HTTP range and compares the hashes with the installed
files; when the changed files take less than 90% of the zip, only their records are downloaded (by
range, from the same zip), each checked against its SHA-256, into <port>/update/new/SotC-PC/. Any
failure (no range support, an older release without the manifest, a hash mismatch) falls back to
the whole zip. Files the installed manifest lists and the new one no longer does are removed.

The files are replaced by a copy of the running launcher (SotC.exe and _internal/ copied to
<port>/update/applier/), started with --apply-update once the launcher has closed, so no file in
use is overwritten. It copies the new files over the port folder, never touching user/ (saves,
shader caches), update/ or an existing sotc.ini, and leaves files the release no longer has in
place. Every replaced file is backed up first; on any error the backups are put back. The
launcher is then started again. update/update.log records what happened.
"""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import struct
import urllib.request
import zipfile
import zlib

RELEASES_URL = 'https://api.github.com/repos/MMadmer/ShadowOfTheColossus-PC/releases/latest'
ASSET_NAME = 'SotC-PC.zip'
# The fork's releases name the zip by version (SotC-PC-1.0.1.zip).
ASSET_PATTERN = re.compile(r'SotC-PC(?:-[\w.]+)?\.zip')
TOP = 'SotC-PC'
KEEP = ('user', 'update')  # top-level names an update never writes into
USER_AGENT = 'SotC-PC-launcher'
MANIFEST = 'update-manifest.txt'
SCHEMA = 'schema=sotc-pc.update/1'
DELTA_WORTH = 0.9      # a delta under 90% of the zip is downloaded instead of the zip
RANGE_GAP = 256 << 10  # records closer than this are fetched in one range


def version_key(text):
    """'0.57-opt5' -> (0, 57, 5); '0.58' -> (0, 58)."""
    return tuple(int(n) for n in re.findall(r'\d+', text or ''))


def _open(url, timeout):
    request = urllib.request.Request(url, headers={'Accept': 'application/vnd.github+json',
                                                   'User-Agent': USER_AGENT})
    return urllib.request.urlopen(request, timeout=timeout)


def latest_release(timeout=10):
    """The latest release with a SotC-PC.zip asset, as a dict, or None."""
    with _open(os.environ.get('SOTC_UPDATE_URL') or RELEASES_URL, timeout) as response:
        data = json.load(response)
    asset = next((a for a in data.get('assets', [])
                  if ASSET_PATTERN.fullmatch(a.get('name') or '')), None)
    if not asset or not data.get('tag_name'):
        return None
    digest = asset.get('digest') or ''
    return {'version': data['tag_name'].lstrip('vV'), 'name': data.get('name') or data['tag_name'],
            'notes': data.get('body') or '', 'page': data.get('html_url') or '',
            'url': asset['browser_download_url'], 'size': int(asset.get('size') or 0),
            'sha256': digest[7:].lower() if digest.startswith('sha256:') else ''}


def is_newer(release, current):
    return bool(release) and version_key(release['version']) > version_key(current)


def download(release, update_dir, progress=None, plan=None):
    """The changed files of the release (plan_delta) or, without a plan or when the delta fails,
    the whole zip, checked and unpacked; returns the new SotC-PC folder."""
    update_dir.mkdir(parents=True, exist_ok=True)
    if plan:
        try:
            return download_delta(plan, update_dir, progress)
        except Exception as error:  # the whole zip instead
            with open(update_dir / 'update.log', 'a', encoding='utf-8') as log:
                log.write(time.strftime('%Y-%m-%d %H:%M:%S ') + f'delta failed, whole zip: {error!r}\n')
    part = update_dir / (ASSET_NAME + '.part')
    digest, done = hashlib.sha256(), 0
    with _open(release['url'], 60) as response, open(part, 'wb') as out:
        total = int(response.headers.get('Content-Length') or release['size'] or 0)
        while True:
            chunk = response.read(1 << 20)
            if not chunk:
                break
            out.write(chunk)
            digest.update(chunk)
            done += len(chunk)
            if progress:
                progress(done, total)
    if release['size'] and done != release['size']:
        raise ValueError(f'download incomplete: {done} of {release["size"]} bytes')
    if release['sha256'] and digest.hexdigest() != release['sha256']:
        raise ValueError('download damaged: SHA-256 does not match the release')
    new = update_dir / 'new'
    shutil.rmtree(new, ignore_errors=True)
    with zipfile.ZipFile(part) as archive:
        names = archive.namelist()
        if f'{TOP}/SotC.exe' not in names:
            raise ValueError(f'not a SotC-PC release (no {TOP}/SotC.exe)')
        for name in names:
            parts = Path(name).parts
            if name.startswith(('/', '\\')) or ':' in name or '..' in parts or parts[0] != TOP:
                raise ValueError(f'unexpected path in the release: {name}')
        if archive.testzip() is not None:
            raise ValueError('the release zip is damaged')
        archive.extractall(new)
    part.unlink()
    return new / TOP


def read_manifest(text, version=None):
    """{relative path: (sha256, size)} of an update-manifest.txt; with version, it must match."""
    lines = text.splitlines()
    if len(lines) < 2 or lines[0] != SCHEMA or not lines[1].startswith('version='):
        raise ValueError('not an update manifest')
    if version and lines[1][len('version='):] != version:
        raise ValueError(f'manifest of version {lines[1][8:]}, release {version}')
    rows = {}
    for line in lines[2:]:
        sha, size, rel = line.split('\t')
        parts = Path(rel).parts
        if (len(sha) != 64 or not parts or rel.startswith(('/', '\\')) or ':' in rel or '..' in parts
                or parts[0] in KEEP):
            raise ValueError(f'unexpected manifest row: {line!r}')
        rows[rel] = (sha.lower(), int(size))
    return rows


def _file_sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            digest.update(chunk)
    return digest.hexdigest()


def _range(url, start, end, timeout=60):
    """A response with bytes [start, end) of url; the server must answer with that range."""
    request = urllib.request.Request(url, headers={'User-Agent': USER_AGENT,
                                                   'Range': f'bytes={start}-{end - 1}'})
    response = urllib.request.urlopen(request, timeout=timeout)
    if response.status != 206:
        response.close()
        raise ValueError('the server does not send byte ranges')
    return response


def _central_directory(url, size):
    """(url after redirects, {name: record}, offset of the central directory) of a remote zip."""
    tail_size = min(size, 22 + 65535)
    with _range(url, size - tail_size, size) as response:
        url = response.geturl()  # GitHub redirects to its storage: the next ranges go there
        tail = response.read()
    at = tail.rfind(b'PK\x05\x06')
    if at < 0 or len(tail) - at < 22:
        raise ValueError('no zip directory at the end of the release')
    _sig, _disk, _cd_disk, _here, count, cd_size, cd_offset, _comment = struct.unpack_from(
        '<IHHHHIIH', tail, at)
    if count == 0xFFFF or 0xFFFFFFFF in (cd_size, cd_offset) or cd_size > 8 << 20:
        raise ValueError('zip64 or oversized zip directory')
    tail_start = size - tail_size
    if cd_offset >= tail_start:
        directory = tail[cd_offset - tail_start:cd_offset - tail_start + cd_size]
    else:
        with _range(url, cd_offset, cd_offset + cd_size) as response:
            directory = response.read()
    records, pos = {}, 0
    for _ in range(count):
        (sig, _made, _need, flags, method, _time, _date, crc, csize, usize, name_len, extra_len,
         comment_len, _disk, _iattr, _eattr, offset) = struct.unpack_from(
            '<IHHHHHHIIIHHHHHII', directory, pos)
        if sig != 0x02014B50:
            raise ValueError('damaged zip directory')
        name = directory[pos + 46:pos + 46 + name_len].decode('utf-8')
        pos += 46 + name_len + extra_len + comment_len
        if flags & 1 or method not in (0, 8):
            raise ValueError(f'unsupported zip record {name}')
        records[name] = {'method': method, 'crc': crc, 'csize': csize, 'usize': usize,
                         'offset': offset}
    # Each record spans from its local header to the next one (or the directory).
    starts = sorted(r['offset'] for r in records.values()) + [cd_offset]
    following = dict(zip(starts, starts[1:]))
    for record in records.values():
        record['end'] = following[record['offset']]
    return url, records, cd_offset


def _extract(buffer, at, record):
    """The uncompressed bytes of a zip record whose local header is at buffer[at]."""
    sig, _ver, _flags, _method, _time, _date, _crc, _cs, _us, name_len, extra_len = struct.unpack_from(
        '<IHHHHHIIIHH', buffer, at)
    if sig != 0x04034B50:
        raise ValueError('damaged zip record')
    start = at + 30 + name_len + extra_len
    data = bytes(buffer[start:start + record['csize']])
    if record['method'] == 8:
        data = zlib.decompressobj(-15).decompress(data)
    if len(data) != record['usize'] or zlib.crc32(data) != record['crc']:
        raise ValueError('damaged zip record')
    return data


def plan_delta(release, port_dir):
    """The files of the release that differ from the installed ones, as a download plan, or None
    when the whole zip is as cheap. Raises when the release cannot be read by range."""
    port_dir = Path(port_dir)
    size = release['size']
    if not size:
        raise ValueError('release size unknown')
    url, records, _ = _central_directory(release['url'], size)
    manifest_record = records.get(f'{TOP}/{MANIFEST}')
    if not manifest_record:
        raise ValueError('the release has no update manifest')
    with _range(url, manifest_record['offset'], manifest_record['end']) as response:
        manifest = _extract(response.read(), 0, manifest_record)
    rows = read_manifest(manifest.decode('utf-8'), release['version'])
    files = []
    for rel, (sha, file_size) in rows.items():
        local = port_dir / rel
        if rel == 'sotc.ini' and local.exists():
            continue  # the player's own settings stay
        if local.is_file() and local.stat().st_size == file_size and _file_sha256(local) == sha:
            continue
        record = records.get(f'{TOP}/{rel}')
        if not record or record['usize'] != file_size:
            raise ValueError(f'{rel} is not in the release zip')
        files.append((rel, sha, record))
    files.sort(key=lambda f: f[2]['offset'])
    spans = []  # [start, end, [files]]
    for item in files:
        record = item[2]
        if spans and record['offset'] - spans[-1][1] < RANGE_GAP:
            spans[-1][1] = record['end']
            spans[-1][2].append(item)
        else:
            spans.append([record['offset'], record['end'], [item]])
    total = sum(end - start for start, end, _ in spans)
    if total >= size * DELTA_WORTH:
        return None
    return {'url': url, 'spans': spans, 'bytes': total, 'files': len(files), 'manifest': manifest}


def download_delta(plan, update_dir, progress=None):
    """Downloads the plan's records into update/new/SotC-PC, each checked against the manifest;
    returns that folder. A dropped connection resumes where it stopped (5 tries per range)."""
    new = update_dir / 'new'
    shutil.rmtree(new, ignore_errors=True)
    root = new / TOP
    root.mkdir(parents=True)
    done = 0
    for start, end, items in plan['spans']:
        buffer, failures = bytearray(), 0
        while len(buffer) < end - start:
            try:
                with _range(plan['url'], start + len(buffer), end) as response:
                    while chunk := response.read(1 << 20):
                        buffer += chunk
                        done += len(chunk)
                        if progress:
                            progress(done, plan['bytes'])
                if len(buffer) < end - start:
                    raise ConnectionError('connection closed early')
            except (OSError, ValueError):
                failures += 1
                if failures >= 5:
                    raise
                time.sleep(0.5 * (1 << failures))
        for rel, sha, record in items:
            data = _extract(buffer, record['offset'] - start, record)
            if hashlib.sha256(data).hexdigest() != sha:
                raise ValueError(f'{rel}: SHA-256 does not match the manifest')
            target = root / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
    (root / MANIFEST).write_bytes(plan['manifest'])
    return root


def start_applier(new_root, port_dir):
    """Copies the running launcher to update/applier and starts it with --apply-update; the
    caller then closes. Only for the frozen launcher (SotC.exe + _internal/)."""
    exe = Path(sys.executable).resolve()
    applier = port_dir / 'update' / 'applier'
    shutil.rmtree(applier, ignore_errors=True)
    applier.mkdir(parents=True)
    shutil.copy2(exe, applier / exe.name)
    internal = exe.parent / '_internal'
    if internal.is_dir():
        shutil.copytree(internal, applier / '_internal')
    flags = getattr(subprocess, 'DETACHED_PROCESS', 0) | getattr(subprocess, 'CREATE_NEW_PROCESS_GROUP', 0)
    subprocess.Popen([str(applier / exe.name), '--apply-update', str(new_root), str(port_dir),
                      str(os.getpid())], cwd=str(applier), creationflags=flags, close_fds=True)


def _wait_for_exit(pid, seconds=60):
    if not pid or os.name != 'nt':
        return
    SYNCHRONIZE = 0x00100000
    handle = ctypes.windll.kernel32.OpenProcess(SYNCHRONIZE, False, pid)
    if handle:
        ctypes.windll.kernel32.WaitForSingleObject(handle, seconds * 1000)
        ctypes.windll.kernel32.CloseHandle(handle)


def _copy(src, dst):
    # A file may stay locked for a moment (antivirus scan of the new exe): retry for ~10 s.
    for attempt in range(20):
        try:
            shutil.copy2(src, dst)
            return
        except PermissionError:
            if attempt == 19:
                raise
            time.sleep(0.5)


def apply_update(new_root, port_dir, pid=0, restart=True):
    """Copies new_root over port_dir (see the module docstring). Returns True on success."""
    new_root, port_dir = Path(new_root), Path(port_dir)
    update_dir = port_dir / 'update'
    update_dir.mkdir(parents=True, exist_ok=True)
    log = open(update_dir / 'update.log', 'a', encoding='utf-8')

    def note(text):
        log.write(time.strftime('%Y-%m-%d %H:%M:%S ') + text + '\n')
        log.flush()

    _wait_for_exit(pid)
    backup = update_dir / 'backup'
    shutil.rmtree(backup, ignore_errors=True)
    done = []  # (installed file, its backup or None)
    ok = False
    try:
        if not (new_root / 'SotC.exe').is_file() and not (new_root / MANIFEST).is_file():
            raise ValueError(f'{new_root} is not a SotC-PC folder')
        # Files of the installed version that the new one no longer has (both manifests known).
        gone = []
        if (new_root / MANIFEST).is_file() and (port_dir / MANIFEST).is_file():
            new_rows = read_manifest((new_root / MANIFEST).read_text(encoding='utf-8'))
            old_rows = read_manifest((port_dir / MANIFEST).read_text(encoding='utf-8'))
            gone = [rel for rel in old_rows if rel not in new_rows and rel != 'sotc.ini']
        for rel in gone:
            dst = port_dir / rel
            if dst.is_file():
                saved = backup / rel
                saved.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(dst, saved)
                done.append((dst, saved))
                dst.unlink()
        for src in sorted(p for p in new_root.rglob('*') if p.is_file()):
            rel = src.relative_to(new_root)
            if rel.parts[0] in KEEP or (rel == Path('sotc.ini') and (port_dir / rel).exists()):
                continue
            dst = port_dir / rel
            saved = None
            if dst.exists():
                saved = backup / rel
                saved.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(dst, saved)
            done.append((dst, saved))
            dst.parent.mkdir(parents=True, exist_ok=True)
            _copy(src, dst)
        note(f'updated: {len(done) - len(gone)} files from {new_root}, removed {len(gone)}')
        ok = True
    except Exception as error:  # put every replaced file back
        note(f'FAILED: {error!r}; restoring {len(done)} files')
        for dst, saved in reversed(done):
            try:
                if saved:
                    _copy(saved, dst)
                elif dst.exists():
                    dst.unlink()
            except OSError as restore_error:
                note(f'could not restore {dst}: {restore_error!r}')
        (update_dir / 'failed').write_text(str(error), encoding='utf-8')
    log.close()
    if restart and (port_dir / 'SotC.exe').is_file():
        flags = getattr(subprocess, 'DETACHED_PROCESS', 0) | getattr(subprocess, 'CREATE_NEW_PROCESS_GROUP', 0)
        subprocess.Popen([str(port_dir / 'SotC.exe')], cwd=str(port_dir), creationflags=flags, close_fds=True)
    return ok


def cleanup(port_dir):
    """At launcher start: the error of a failed update (or None), and update/ removed."""
    update_dir = Path(port_dir) / 'update'
    failed = update_dir / 'failed'
    error = failed.read_text(encoding='utf-8', errors='replace') if failed.is_file() else None
    for name in ('new', 'applier', 'backup', 'failed', ASSET_NAME + '.part'):
        path = update_dir / name
        if path.is_dir():
            shutil.rmtree(path, ignore_errors=True)  # the applier may still be closing
        elif path.exists():
            try:
                path.unlink()
            except OSError:
                pass
    return error

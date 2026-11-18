"""Private installer input validation. Does not download or install anything."""
from pathlib import Path, PurePosixPath
import hashlib
import json
import stat
import zipfile

MAX_UNPACKED = 128 * 1024 * 1024


def verified_vendor(directory, spec):
    path = Path(directory) / spec['filename']
    if not path.is_file():
        raise ValueError(f"Missing private package input: {path}")
    data = path.read_bytes()
    if len(data) != spec['size'] or hashlib.sha256(data).hexdigest() != spec['sha256']:
        raise ValueError(f"Package does not match vendor-lock.json: {path.name}")
    return path


def checked_zip(path):
    """Return unchanged file bytes, rejecting traversal, links and zip bombs."""
    result = []
    seen = set()
    total = 0
    with zipfile.ZipFile(path) as archive:
        for entry in archive.infolist():
            name = entry.filename
            parts = PurePosixPath(name).parts
            if not parts or name.startswith('/') or '\\' in name or ':' in name or any(p in ('.', '..') for p in parts):
                raise ValueError('Unsafe archive member path')
            if name.lower() in seen:
                raise ValueError('Duplicate archive member')
            seen.add(name.lower())
            if stat.S_ISLNK(entry.external_attr >> 16):
                raise ValueError('Archive links are not permitted')
            total += entry.file_size
            if total > MAX_UNPACKED:
                raise ValueError('Package exceeds unpacked size limit')
            if not entry.is_dir():
                result.append((name, archive.read(entry)))
    return result


def write_manifest(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + '\n')

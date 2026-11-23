#!/usr/bin/env python3
"""Package explicit app-only builds and tracked source; never install or publish.

Driver-bundled private installers are deliberately not release inputs. Use a clean
source tree for a release; --allow-dirty is only for development verification.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile
import zipfile

from release_metadata import release_metadata

ROOT = Path(__file__).resolve().parents[1]
ARCHIVES = ('San Audisco-Windows-x64.zip', 'San Audisco-source.zip',
            'San Audisco-macOS-arm64.zip')
OUTPUTS = ('BUILD_INFO.json', *ARCHIVES, 'SHA256SUMS.txt')


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_files(root):
    """Select tracked source only; never infer payloads from directory contents."""
    names = subprocess.check_output(['git', '-C', str(root), 'ls-files', '-z']).decode().split('\0')
    for name in names:
        if not name:
            continue
        path = root / name
        relative = Path(name)
        if any(part in ('.git', 'dist', 'build', 'deps', '__pycache__') for part in relative.parts):
            continue
        if path.suffix.lower() in ('.key', '.conf', '.zip', '.pkg', '.dmg', '.exe', '.dll') or path.name == '.DS_Store':
            continue
        if path.is_symlink() or not path.is_file():
            raise ValueError(f'Tracked source is missing or not a regular file: {name}')
        yield path, relative


def validate_mac_bundle(app, root):
    """Accept only files emitted by package-mac.sh, including source-matched notices.

    A signed .app is a directory, not an opaque safe payload: reject extra
    installers, executables, hidden files, and links before producing an archive.
    """
    if app.is_symlink() or not app.is_dir():
        raise ValueError('Mac app must be a regular bundle directory, not a symlink')
    resources = {
        'Contents/Resources/SanAudisco.icns': root / 'assets/SanAudisco.icns',
        'Contents/Resources/licenses/nlohmann-json.txt': root / 'third_party/json/LICENSE.MIT',
    }
    for name in ('README.md', 'VALIDATION.md', 'HARDWARE_TEST.md', 'PRIVATE_SETUP.md'):
        resources['Contents/Resources/' + name] = root / name
    if (root / 'LICENSE').is_file():
        resources['Contents/Resources/LICENSE'] = root / 'LICENSE'
    for path in (root / 'third_party/licenses').glob('*.txt'):
        resources['Contents/Resources/licenses/' + path.name] = path
    for path in (root / 'docs').glob('*.md'):
        resources['Contents/Resources/docs/' + path.name] = path
    allowed = set(resources) | {
        'Contents/Info.plist', 'Contents/_CodeSignature/CodeResources',
        'Contents/MacOS/SanAudisco', 'Contents/MacOS/bridge-engine',
        'Contents/MacOS/bridge-agent',
    }
    directories = {parent.as_posix() for name in allowed for parent in Path(name).parents}
    found = set()
    for path in app.rglob('*'):
        name = path.relative_to(app).as_posix()
        if path.is_symlink():
            raise ValueError(f'Mac bundle symlinks are not permitted: {name}')
        if path.is_dir() and name in directories:
            continue
        if not path.is_file() or name not in allowed:
            raise ValueError(f'Unexpected Mac bundle payload: {name}')
        found.add(name)
    missing = allowed - found
    if missing:
        raise ValueError('Missing Mac bundle files: ' + ', '.join(sorted(missing)))
    for name, source in resources.items():
        if source.is_symlink() or not source.is_file() or sha256(app / name) != sha256(source):
            raise ValueError(f'Mac bundled resource differs from current source: {name}')
    return {name: sha256(app / name) for name in sorted(allowed)}


def package(dist, allow_dirty=False, root=ROOT):
    version, mac_build = release_metadata(root)
    dist = dist.resolve()
    artifacts = {
        'macOS-arm64-gui': dist / 'San Audisco.app/Contents/MacOS/SanAudisco',
        'macOS-arm64-engine': dist / 'San Audisco.app/Contents/MacOS/bridge-engine',
        'macOS-arm64-agent': dist / 'San Audisco.app/Contents/MacOS/bridge-agent',
        'Windows-x64-gui': dist / 'windows/SanAudisco.exe',
        'Windows-x64-engine': dist / 'windows/san-audisco.exe',
        'Windows-x64-agent': dist / 'windows/bridge-agent.exe',
    }
    for name, path in artifacts.items():
        if not path.is_file() or path.is_symlink():
            raise ValueError(f'Missing or non-regular required build: {name}: {path}')
    existing = [name for name in OUTPUTS if (dist / name).exists()]
    if existing:
        raise ValueError('Refusing to overwrite release artifacts: ' + ', '.join(existing))
    commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
    dirty = bool(subprocess.check_output(['git', '-C', str(root), 'status', '--porcelain'], text=True))
    if dirty and not allow_dirty:
        raise ValueError('Commit the reviewed source before packaging a release (or use --allow-dirty for development only)')
    app = dist / 'San Audisco.app'
    with (app / 'Contents/Info.plist').open('rb') as stream:
        info = plistlib.load(stream)
    if info.get('CFBundleShortVersionString') != version or info.get('CFBundleVersion') != mac_build:
        raise ValueError('Mac app version/build does not match CMake release metadata')
    mac_files = validate_mac_bundle(app, root)
    sources = list(source_files(root))
    manifest = {
        'version': version, 'mac_build': mac_build,
        'source_commit': commit, 'source_dirty': dirty,
        'packaged_at_utc': datetime.now(timezone.utc).isoformat(),
        'package_kind': 'app-only', 'bundled_audio_drivers': False,
        'binary_sha256': {name: sha256(path) for name, path in artifacts.items()},
        'mac_bundle_sha256': mac_files,
        'validation': 'Packaging checks file inputs and metadata only; consult release validation for executed tests and hardware limits.',
    }
    # Assemble all outputs before publishing any; do not include unrelated ZIPs
    # (particularly private vendor bundles) in this release's checksum manifest.
    with tempfile.TemporaryDirectory(prefix='san-audisco-release-', dir=dist) as temporary:
        stage = Path(temporary)
        (stage / 'BUILD_INFO.json').write_text(json.dumps(manifest, indent=2) + '\n')
        with zipfile.ZipFile(stage / ARCHIVES[0], 'w', zipfile.ZIP_DEFLATED) as archive:
            for name in ('SanAudisco.exe', 'san-audisco.exe', 'bridge-agent.exe'):
                archive.write(dist / 'windows' / name, name)
            archive.write(stage / 'BUILD_INFO.json', 'BUILD_INFO.json')
            for name in ('README.md', 'VALIDATION.md', 'HARDWARE_TEST.md', 'PRIVATE_SETUP.md', 'LICENSE'):
                if (root / name).is_file():
                    archive.write(root / name, name)
            for path in sorted((root / 'third_party/licenses').glob('*.txt')):
                archive.write(path, 'licenses/' + path.name)
            archive.write(root / 'third_party/json/LICENSE.MIT', 'licenses/nlohmann-json.txt')
            for path in sorted((root / 'docs').glob('*.md')):
                archive.write(path, 'docs/' + path.name)
        with zipfile.ZipFile(stage / ARCHIVES[1], 'w', zipfile.ZIP_DEFLATED) as archive:
            for path, relative in sources:
                archive.write(path, 'San Audisco/' + relative.as_posix())
        # Copy the approved file list, never the original directory wholesale.
        # Revalidation catches a changed file or a link substituted while staging.
        staged_app = stage / app.name
        for name in mac_files:
            target = staged_app / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(app / name, target, follow_symlinks=False)
        if validate_mac_bundle(staged_app, root) != mac_files:
            raise ValueError('Mac bundle changed while assembling the release')
        subprocess.run(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent',
                        str(staged_app), str(stage / ARCHIVES[2])], check=True)
        checksummed = ('BUILD_INFO.json', *ARCHIVES)
        (stage / 'SHA256SUMS.txt').write_text(''.join(
            sha256(stage / name) + '  ' + name + '\n' for name in checksummed))
        for name in OUTPUTS:
            # Hard-link creation is exclusive and cannot replace an existing file.
            os.link(stage / name, dist / name)
    return [dist / name for name in OUTPUTS]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dist', type=Path, default=ROOT / 'dist')
    parser.add_argument('--allow-dirty', action='store_true', help='Development only; record an uncommitted source tree')
    args = parser.parse_args()
    try:
        for path in package(args.dist, args.allow_dirty):
            print(path)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Release packaging failed: {error}\n')


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Release boundaries using disposable source/build fixtures, never installed apps."""
import importlib.util
import json
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

REAL_RUN = subprocess.run
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
spec = importlib.util.spec_from_file_location('release_package', ROOT / 'scripts/package-release.py')
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class ReleaseBoundaries(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.dist = self.root / 'dist'
        content = {
            '.gitignore': 'dist/\n',
            'CMakeLists.txt': 'project(SanAudisco VERSION 0.4.0 LANGUAGES CXX)\nset(SAN_AUDISCO_MAC_BUILD 17)\n',
            'README.md': 'Application instructions\n',
            'VALIDATION.md': 'Historical validation\n',
            'HARDWARE_TEST.md': 'Physical tests\n',
            'PRIVATE_SETUP.md': 'Private installers\n',
            'LICENSE': 'Project license fixture\n',
            'assets/SanAudisco.icns': 'Icon fixture\n',
            'src/main.cpp': '// source\n',
            'docs/DISTRIBUTION.md': 'Drivers are separate.\n',
            'third_party/licenses/opus.txt': 'Third-party notice\n',
            'third_party/json/LICENSE.MIT': 'JSON notice\n',
        }
        for name, value in content.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(value)
        self.git('init', '-q')
        self.git('add', '.')
        self.git('-c', 'user.name=Packaging Test', '-c', 'user.email=packaging@example.invalid',
                 'commit', '-qm', 'Fixture')
        for name in ('windows/SanAudisco.exe', 'windows/san-audisco.exe', 'windows/bridge-agent.exe',
                     'windows/runtime_fake.exe', 'San Audisco.app/Contents/MacOS/SanAudisco',
                     'San Audisco.app/Contents/MacOS/bridge-engine', 'San Audisco.app/Contents/MacOS/bridge-agent'):
            path = self.dist / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'fixture binary')
        self.plist = self.dist / 'San Audisco.app/Contents/Info.plist'
        self.plist.write_bytes(plistlib.dumps({'CFBundleShortVersionString': '0.4.0', 'CFBundleVersion': '17'}))
        self.app = self.plist.parent.parent
        bundle_resources = {
            'Contents/Resources/SanAudisco.icns': 'assets/SanAudisco.icns',
            'Contents/Resources/licenses/opus.txt': 'third_party/licenses/opus.txt',
            'Contents/Resources/licenses/nlohmann-json.txt': 'third_party/json/LICENSE.MIT',
            'Contents/Resources/docs/DISTRIBUTION.md': 'docs/DISTRIBUTION.md',
        }
        for name in ('README.md', 'VALIDATION.md', 'HARDWARE_TEST.md', 'PRIVATE_SETUP.md', 'LICENSE'):
            bundle_resources['Contents/Resources/' + name] = name
        for destination, source in bundle_resources.items():
            path = self.app / destination
            path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(self.root / source, path)
        signature = self.app / 'Contents/_CodeSignature/CodeResources'
        signature.parent.mkdir()
        signature.write_text('Signature fixture: file boundary tests do not verify signatures.\n')

    def git(self, *args):
        return subprocess.check_output(['git', '-C', str(self.root), *args], stderr=subprocess.STDOUT)

    @staticmethod
    def ditto_fixture(command, **kwargs):
        if command[0] != 'ditto':
            return REAL_RUN(command, **kwargs)
        app = Path(command[-2])
        with zipfile.ZipFile(command[-1], 'w') as archive:
            for path in app.rglob('*'):
                if path.is_file():
                    archive.write(path, app.name + '/' + path.relative_to(app).as_posix())

    def package(self, **kwargs):
        with patch.object(release.subprocess, 'run', self.ditto_fixture):
            return release.package(self.dist, root=self.root, **kwargs)

    def test_unrelated_private_archives_and_test_binaries_never_enter_release(self):
        private = self.dist / 'Private-Driver-Bundle.zip'
        private.write_bytes(b'private vendor material')
        self.package()
        checksums = (self.dist / 'SHA256SUMS.txt').read_text()
        self.assertNotIn(private.name, checksums)
        self.assertEqual(private.read_bytes(), b'private vendor material')
        with zipfile.ZipFile(self.dist / 'SanAudisco-Windows-x64.zip') as archive:
            names = archive.namelist()
            self.assertNotIn('runtime_fake.exe', names)
            self.assertIn('LICENSE', names)
            self.assertIn('licenses/opus.txt', names)
        with zipfile.ZipFile(self.dist / 'SanAudisco-source.zip') as archive:
            self.assertIn('San Audisco/src/main.cpp', archive.namelist())
            self.assertFalse(any('/dist/' in name for name in archive.namelist()))
        info = json.loads((self.dist / 'BUILD_INFO.json').read_text())
        self.assertFalse(info['source_dirty'])
        self.assertFalse(info['bundled_audio_drivers'])
        with zipfile.ZipFile(self.dist / 'SanAudisco-macOS-arm64.zip') as archive:
            self.assertIn('San Audisco.app/Contents/MacOS/SanAudisco', archive.namelist())
            self.assertIn('San Audisco.app/Contents/Resources/LICENSE', archive.namelist())
            self.assertEqual(set(archive.namelist()),
                             {'San Audisco.app/' + name for name in info['mac_bundle_sha256']})

    def test_private_payload_inside_mac_app_is_rejected(self):
        for name in ('Contents/Resources/drivers/vendor.pkg',
                     'Contents/MacOS/runtime_fake', 'Contents/Resources/.hidden-private.zip'):
            with self.subTest(payload=name):
                path = self.app / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'not part of the app-only release')
                with self.assertRaisesRegex(ValueError, 'Unexpected Mac bundle payload'):
                    self.package()
                self.assertFalse((self.dist / 'BUILD_INFO.json').exists())
                path.unlink()
                if path.parent.name == 'drivers':
                    path.parent.rmdir()

    def test_mac_bundle_symlinks_are_rejected(self):
        path = self.app / 'Contents/Resources/README.md'
        path.unlink()
        path.symlink_to(self.root / 'README.md')
        with self.assertRaisesRegex(ValueError, 'symlinks are not permitted'):
            self.package()
        self.assertFalse((self.dist / 'BUILD_INFO.json').exists())

    def test_missing_mac_license_is_rejected(self):
        (self.app / 'Contents/Resources/licenses/opus.txt').unlink()
        with self.assertRaisesRegex(ValueError, 'Missing Mac bundle files'):
            self.package()
        self.assertFalse((self.dist / 'BUILD_INFO.json').exists())

    def test_stale_or_replaced_bundled_document_is_rejected(self):
        (self.app / 'Contents/Resources/README.md').write_bytes(b'private payload in a known filename')
        with self.assertRaisesRegex(ValueError, 'resource differs from current source'):
            self.package()
        self.assertFalse((self.dist / 'BUILD_INFO.json').exists())

    def test_existing_artifact_is_not_overwritten(self):
        artifact = self.dist / 'SanAudisco-source.zip'
        artifact.write_bytes(b'previous release')
        with self.assertRaisesRegex(ValueError, 'overwrite'):
            self.package()
        self.assertEqual(artifact.read_bytes(), b'previous release')
        self.assertFalse((self.dist / 'BUILD_INFO.json').exists())

    def test_version_mismatch_fails_before_creating_release(self):
        self.plist.write_bytes(plistlib.dumps({'CFBundleShortVersionString': '0.3.3', 'CFBundleVersion': '16'}))
        with self.assertRaisesRegex(ValueError, 'version/build'):
            self.package()
        self.assertFalse((self.dist / 'BUILD_INFO.json').exists())

    def test_uncommitted_source_requires_explicit_development_mode(self):
        (self.root / 'src/main.cpp').write_text('// changed\n')
        with self.assertRaisesRegex(ValueError, 'Commit the reviewed source'):
            self.package()
        self.package(allow_dirty=True)
        self.assertTrue(json.loads((self.dist / 'BUILD_INFO.json').read_text())['source_dirty'])

    def test_missing_tracked_source_cannot_silently_disappear(self):
        (self.root / 'src/main.cpp').unlink()
        with self.assertRaisesRegex(ValueError, 'Tracked source is missing'):
            self.package(allow_dirty=True)
        self.assertFalse((self.dist / 'BUILD_INFO.json').exists())


if __name__ == '__main__':
    unittest.main()

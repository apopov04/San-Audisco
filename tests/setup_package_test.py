#!/usr/bin/env python3
"""Package-boundary tests: no drivers, audio devices, or elevation involved."""
from pathlib import Path
import hashlib
import json
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'installer'))
from package_support import checked_zip, verified_vendor


class PackageInputs(unittest.TestCase):
    def test_hash_and_size_both_required(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'vendor.pkg';path.write_bytes(b'expected')
            spec={'filename':path.name,'size':8,'sha256':hashlib.sha256(b'expected').hexdigest()}
            self.assertEqual(verified_vendor(directory,spec),path)
            path.write_bytes(b'changed!')
            with self.assertRaises(ValueError):verified_vendor(directory,spec)
            path.write_bytes(b'expected\x00')
            with self.assertRaises(ValueError):verified_vendor(directory,spec)

    def archive(self,entries):
        directory=tempfile.TemporaryDirectory();self.addCleanup(directory.cleanup)
        path=Path(directory.name)/'vendor.zip'
        with zipfile.ZipFile(path,'w') as z:
            for name,data in entries:z.writestr(name,data)
        return path

    def test_original_bytes_are_preserved(self):
        rows=[('setup.exe',b'\x00\xffbinary'),('license.txt',b'copyright\r\n')]
        self.assertEqual(checked_zip(self.archive(rows)),rows)

    def test_archive_path_boundaries(self):
        for name in ('../escape','/absolute','nested/../../escape','C:/file','x\\..\\file'):
            with self.subTest(name=name),self.assertRaises(ValueError):checked_zip(self.archive([(name,b'x')]))

    def test_case_collisions_are_rejected_for_windows(self):
        with self.assertRaises(ValueError):checked_zip(self.archive([('SETUP.exe',b'x'),('setup.exe',b'y')]))

    def test_links_rejected(self):
        info=zipfile.ZipInfo('setup.exe');info.create_system=3;info.external_attr=(0o120777<<16)
        with self.assertRaises(ValueError):checked_zip(self.archive([(info,b'outside')]))

    def test_expansion_budget(self):
        import package_support
        original=package_support.MAX_UNPACKED
        try:
            package_support.MAX_UNPACKED=5
            with self.assertRaises(ValueError):checked_zip(self.archive([('a',b'123'),('b',b'456')]))
        finally:package_support.MAX_UNPACKED=original

    def test_lock_has_all_independent_components(self):
        lock=json.loads((ROOT/'installer/vendor-lock.json').read_text())
        self.assertEqual(set(lock['components']),{'vbcable','hifi','blackhole2','blackhole16'})
        for spec in lock['components'].values():
            self.assertEqual(len(bytes.fromhex(spec['sha256'])),32)
            self.assertTrue(spec['source'].startswith('https://'))


if __name__=='__main__':unittest.main()

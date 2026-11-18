#!/usr/bin/env python3
"""Package-boundary tests: no drivers, audio devices, or elevation involved."""
from pathlib import Path
import hashlib
import json
import os
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


@unittest.skipUnless(sys.platform == 'win32' and os.environ.get('SAN_AUDISCO_SETUP_UI_EXE'),
                     'Set SAN_AUDISCO_SETUP_UI_EXE on Windows/Wine for native setup checks')
class WindowsSetupUI(unittest.TestCase):
    def test_guided_setup_layout_and_checks(self):
        # Never clicks an install action: only Welcome, Details, and Check.
        import ctypes as c
        import ctypes.wintypes as w
        import subprocess
        import time
        u=c.WinDLL('user32',use_last_error=True)
        u.FindWindowW.argtypes=[w.LPCWSTR,w.LPCWSTR];u.FindWindowW.restype=w.HWND
        u.GetDlgItem.argtypes=[w.HWND,c.c_int];u.GetDlgItem.restype=w.HWND
        u.SendMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM];u.SendMessageW.restype=w.LPARAM
        u.GetWindowRect.argtypes=[w.HWND,c.POINTER(w.RECT)]
        u.IsWindowVisible.argtypes=[w.HWND];u.IsWindowVisible.restype=w.BOOL
        u.IsWindowEnabled.argtypes=[w.HWND];u.IsWindowEnabled.restype=w.BOOL
        u.SetWindowPos.argtypes=[w.HWND,w.HWND,c.c_int,c.c_int,c.c_int,c.c_int,w.UINT]
        def wait(predicate):
            until=time.monotonic()+20
            while time.monotonic()<until:
                if predicate():return
                time.sleep(.05)
            self.fail('Native setup did not reach expected state')
        def value(handle):
            text=c.create_unicode_buffer(4096)
            u.SendMessageW(handle,13,len(text),c.cast(text,c.c_void_p).value)
            return text.value
        self.assertFalse(u.FindWindowW('SanAudiscoPrivateSetup',None),'Existing setup must be closed first')
        process=subprocess.Popen([os.environ['SAN_AUDISCO_SETUP_UI_EXE']])
        try:
            wait(lambda:u.FindWindowW('SanAudiscoPrivateSetup',None))
            window=u.FindWindowW('SanAudiscoPrivateSetup',None)
            get=lambda control:u.GetDlgItem(window,control)
            wait(lambda:value(get(100))=='Check this computer')
            self.assertFalse(u.IsWindowVisible(get(251)))
            self.assertTrue(u.SendMessageW(get(200),0x171,0,0),'Brand icon is missing')
            def bounds(control):
                rect=w.RECT();self.assertTrue(u.GetWindowRect(get(control),c.byref(rect)));return rect
            for width,height in [(800,700),(1000,850),(680,500)]:
                u.SetWindowPos(window,None,0,0,width,height,0x16)
                time.sleep(.1)
                self.assertLessEqual(bounds(200).right,bounds(201).left)
                for row in range(3):self.assertLessEqual(bounds(220+row).right,bounds(240+row).left)
                self.assertLessEqual(bounds(250).bottom,bounds(110).top)
            u.SetWindowPos(window,None,0,0,900,950,0x16)
            u.SendMessageW(get(110),0xf5,0,0)
            self.assertTrue(u.IsWindowVisible(get(251)))
            self.assertLessEqual(bounds(251).bottom,bounds(106).top)
            self.assertLessEqual(bounds(108).bottom,bounds(100).top)
            u.SendMessageW(get(110),0xf5,0,0)
            u.SendMessageW(get(100),0xf5,0,0)
            wait(lambda:u.IsWindowEnabled(get(100)))
            self.assertIn(value(get(100)),('Install / update San Audisco','Install playback component',
                'Install microphone component','Open San Audisco','Check again'))
            self.assertNotEqual(value(get(240)),'Not checked')
            u.SendMessageW(window,0x10,0,0);process.wait(timeout=5)
        finally:
            if process.poll() is None:process.terminate();process.wait(timeout=5)


if __name__=='__main__':unittest.main()

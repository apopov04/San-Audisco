#!/usr/bin/env python3
"""Build a private offline setup artifact from explicit, locally supplied inputs.

Never installs drivers, launches San Audisco, downloads dependencies, or publishes
an artifact. Vendor packages are intentionally excluded from source control.
"""
import argparse
import importlib.util
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'installer'))
from package_support import checked_zip, verified_vendor, write_manifest
from release_metadata import release_metadata

VERSION, MAC_BUILD = release_metadata()


def run(args):
    subprocess.run([str(x) for x in args], check=True)


def files_under(path):
    for p in sorted(path.rglob('*')):
        if p.is_symlink():
            raise ValueError(f'Symlink payloads require explicit review: {p}')
        if p.is_file():
            yield p


def build_mac(args, lock, scratch):
    app = args.app.resolve()
    for executable in ('SanAudisco', 'bridge-engine', 'bridge-agent'):
        if not (app / 'Contents/MacOS' / executable).is_file():
            raise ValueError(f'Missing app executable: {executable}')
    run(['/usr/bin/codesign', '--verify', '--deep', '--strict', app])
    setup = scratch / 'San Audisco Setup.app'
    contents = setup / 'Contents'
    resources = contents / 'Resources'
    (contents / 'MacOS').mkdir(parents=True)
    (resources / 'drivers').mkdir(parents=True)
    shutil.copytree(app, resources / 'San Audisco.app')
    for component in ('blackhole2', 'blackhole16'):
        package = verified_vendor(args.vendor_dir, lock['components'][component])
        signature = subprocess.check_output(['/usr/sbin/pkgutil', '--check-signature', str(package)], text=True)
        if 'Q5C99V536K' not in signature or 'signed by a developer certificate issued by Apple' not in signature:
            raise ValueError('BlackHole package publisher could not be verified')
        shutil.copy2(package, resources / 'drivers' / package.name)
    shutil.copy2(ROOT / 'PRIVATE_SETUP.md', resources / 'PRIVATE_SETUP.md')
    shutil.copy2(ROOT/'assets/SanAudisco.icns', resources/'SanAudisco.icns')
    rows = [{'path':p.relative_to(resources).as_posix(), 'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in files_under(resources)]
    write_manifest(resources / 'manifest.json', {'schema':1, 'private_only':True, 'files':rows})
    with (contents / 'Info.plist').open('wb') as f:
        plistlib.dump({'CFBundleIdentifier':'local.audiobridge.setup', 'CFBundleExecutable':'SanAudiscoSetup', 'CFBundleName':'San Audisco Setup', 'CFBundleIconFile':'SanAudisco', 'CFBundleVersion':MAC_BUILD, 'CFBundleShortVersionString':VERSION, 'LSMinimumSystemVersion':'13.0', 'NSHighResolutionCapable':True}, f)
    run(['clang++', '-target', 'arm64-apple-macos13.0', '-std=c++20', '-O2', '-c', ROOT/'installer/setup_policy.cpp', '-o',scratch/'setup_policy.o'])
    run(['swiftc', '-target', 'arm64-apple-macos13.0', '-O', '-parse-as-library', ROOT / 'installer/macos/Setup.swift', scratch/'setup_policy.o', '-o', contents / 'MacOS/SanAudiscoSetup', '-framework', 'SwiftUI', '-framework', 'AppKit', '-framework', 'CoreAudio', '-framework', 'CryptoKit'])
    spec = importlib.util.spec_from_file_location('sign_mac', ROOT/'scripts/sign-mac.py')
    signing = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(signing)
    signing.sign(setup)
    run(['/usr/bin/codesign', '--verify', '--deep', '--strict', setup])
    args.output.mkdir(parents=True, exist_ok=True)
    artifact = args.output / 'San Audisco-Private-Setup-macOS-arm64.dmg'
    if artifact.exists():
        raise ValueError('Refusing to overwrite an existing installer artifact')
    disk = scratch / 'disk'; disk.mkdir()
    shutil.copytree(setup, disk / setup.name)
    shutil.copy2(ROOT / 'PRIVATE_SETUP.md', disk / 'READ-ME.txt')
    run(['/usr/bin/hdiutil', 'create', '-volname', 'San Audisco Setup', '-srcfolder', disk, '-format', 'UDZO', artifact])
    return artifact


def build_windows(args, lock, scratch):
    if not args.toolchain:
        raise ValueError('--toolchain must name the LLVM-MinGW bin directory')
    entries = []
    for executable in ('SanAudisco.exe', 'san-audisco.exe', 'bridge-agent.exe'):
        path = args.app / executable
        if not path.is_file():
            raise ValueError(f'Missing app executable: {path}')
        entries.append(('app', executable, path.read_bytes()))
    entries.append(('app', 'PRIVATE_SETUP.md', (ROOT / 'PRIVATE_SETUP.md').read_bytes()))
    for name in ('README.md', 'VALIDATION.md', 'HARDWARE_TEST.md'):
        entries.append(('app', name, (ROOT / name).read_bytes()))
    for document in sorted((ROOT / 'docs').glob('*.md')):
        entries.append(('app', 'docs/' + document.name, document.read_bytes()))
    entries.append(('app', 'licenses/nlohmann-json.txt', (ROOT / 'third_party/json/LICENSE.MIT').read_bytes()))
    for path in sorted((ROOT / 'third_party/licenses').glob('*')):
        if path.is_file():
            entries.append(('app', 'licenses/' + path.name, path.read_bytes()))
    for component in ('vbcable', 'hifi'):
        package = verified_vendor(args.vendor_dir, lock['components'][component])
        entries.extend((component,name,data) for name,data in checked_zip(package))
    header = ['#pragma once', 'struct Payload {int id; const wchar_t* group; const wchar_t* path; const char* sha256;};', 'inline const Payload payload[] = {']
    shutil.copy2(ROOT/'assets/SanAudisco.ico', scratch/'SanAudisco.ico')
    rc = ['#include <windows.h>', '1 RT_MANIFEST "setup.manifest"', '101 ICON "SanAudisco.ico"', '''1 VERSIONINFO
FILEVERSION @VERSION_NUMBERS@
PRODUCTVERSION @VERSION_NUMBERS@
FILEOS VOS_NT_WINDOWS32
FILETYPE VFT_APP
BEGIN
 BLOCK "StringFileInfo"
 BEGIN
  BLOCK "040904b0"
  BEGIN
   VALUE "FileDescription", "San Audisco Setup\\0"
   VALUE "ProductName", "San Audisco\\0"
   VALUE "FileVersion", "@VERSION@\\0"
   VALUE "ProductVersion", "@VERSION@\\0"
  END
 END
 BLOCK "VarFileInfo"
 BEGIN
  VALUE "Translation", 0x409, 1200
 END
END'''.replace('@VERSION_NUMBERS@', VERSION.replace('.', ',')+',0').replace('@VERSION@', VERSION)]
    manifest = []
    for number, (group, name, data) in enumerate(entries, 100):
        staged = scratch / f'payload-{number}.bin'; staged.write_bytes(data)
        digest = hashlib.sha256(data).hexdigest()
        # Paths are locked archive members or repository-controlled app files.
        header.append('{%d,L%s,L%s,%s},' % (number,json.dumps(group),json.dumps(name),json.dumps(digest)))
        rc.append(f'{number} RCDATA "payload-{number}.bin"')
        manifest.append({'group':group,'path':name,'sha256':digest,'bytes':len(data)})
    header.append('};')
    (scratch / 'payload.hpp').write_text('\n'.join(header)+'\n')
    (scratch / 'setup.rc').write_text('\n'.join(rc)+'\n')
    (scratch / 'setup.manifest').write_text('''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0"><dependency><dependentAssembly><assemblyIdentity type="win32" name="Microsoft.Windows.Common-Controls" version="6.0.0.0" processorArchitecture="*" publicKeyToken="6595b64144ccf1df" language="*"/></dependentAssembly></dependency><trustInfo xmlns="urn:schemas-microsoft-com:asm.v3"><security><requestedPrivileges><requestedExecutionLevel level="asInvoker" uiAccess="false"/></requestedPrivileges></security></trustInfo><compatibility xmlns="urn:schemas-microsoft-com:compatibility.v1"><application><supportedOS Id="{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}"/></application></compatibility></assembly>''')
    toolchain = args.toolchain.resolve()
    subprocess.run([str(toolchain/'x86_64-w64-mingw32-windres'), 'setup.rc', '-O','coff','-o','setup.res'], cwd=scratch,check=True)
    args.output.mkdir(parents=True,exist_ok=True)
    artifact = args.output / 'San Audisco-Private-Setup-Windows-x64.exe'
    if artifact.exists():
        raise ValueError('Refusing to overwrite an existing installer artifact')
    run([toolchain/'x86_64-w64-mingw32-clang++', '-std=c++20','-O2','-municode','-mwindows','-static','-DUNICODE','-D_UNICODE','-I',ROOT/'installer', '-I',scratch, ROOT/'installer/windows/setup.cpp',ROOT/'installer/setup_policy.cpp',scratch/'setup.res','-o',artifact,'-lbcrypt','-lwintrust','-lcrypt32','-lshell32','-lole32','-luuid','-lpropsys','-lgdi32'])
    write_manifest(args.output/'WINDOWS_PRIVATE_PAYLOAD.json',{'schema':1,'private_only':True,'files':manifest})
    return artifact


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=('mac','windows'),required=True)
    parser.add_argument('--app',type=Path,required=True,help='Mac .app or Windows executable directory')
    parser.add_argument('--vendor-dir',type=Path,required=True,help='Private directory containing exact vendor-lock.json packages')
    parser.add_argument('--output',type=Path,required=True,help='Private output directory; never publish these artifacts')
    parser.add_argument('--toolchain',type=Path)
    args = parser.parse_args();args.app=args.app.resolve();args.output=args.output.resolve();args.vendor_dir=args.vendor_dir.resolve()
    # Prevent vendor material from entering tracked source directories accidentally.
    if args.output.is_relative_to(ROOT) and not args.output.is_relative_to(ROOT/'dist'):
        raise ValueError('Use an output outside the source repository, or its ignored dist directory')
    lock=json.loads((ROOT/'installer/vendor-lock.json').read_text())
    with tempfile.TemporaryDirectory(prefix='san-audisco-private-') as directory:
        scratch=Path(directory)
        artifact=(build_mac if args.platform=='mac' else build_windows)(args,lock,scratch)
    digest=hashlib.sha256(artifact.read_bytes()).hexdigest()
    (artifact.with_suffix(artifact.suffix+'.sha256')).write_text(digest+'  '+artifact.name+'\n')
    print(artifact)
    print(digest)


if __name__=='__main__':
    try: main()
    except (ValueError,OSError,subprocess.CalledProcessError) as error:
        print(f'Private packaging failed: {error}',file=sys.stderr)
        sys.exit(1)

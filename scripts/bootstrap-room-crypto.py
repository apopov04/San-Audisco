#!/usr/bin/env python3
"""Fetch a pinned BoringSSL source archive and build only its static crypto target.

Usage: bootstrap-room-crypto.py PREFIX [--toolchain LLVM_MINGW_ROOT] [--cmake PATH]
The prefix is build-only; no network access or extra runtime is needed by San Audisco.
"""
import argparse
import hashlib
import json
import platform
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

COMMIT = 'ebd922adc708a48b0cccfcdf81e854a3fdc1b7b6'
SHA256 = '75ee588268ae3d7552f5be7e0849bdcc8e4159f22547dbfb4aae997784fdd345'
URL = f'https://github.com/google/boringssl/archive/{COMMIT}.tar.gz'

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('prefix', type=Path)
    parser.add_argument('--toolchain', type=Path)
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    prefix = args.prefix.resolve()
    work = prefix.parent / 'room-crypto-source'
    work.mkdir(parents=True, exist_ok=True)
    archive = work / f'boringssl-{COMMIT}.tar.gz'
    if not archive.exists():
        temporary = archive.with_suffix('.download')
        with urllib.request.urlopen(URL, timeout=90) as response, temporary.open('wb') as output:
            shutil.copyfileobj(response, output)
        temporary.replace(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        raise SystemExit('BoringSSL archive digest mismatch; refusing to build')
    source = work / f'boringssl-{COMMIT}'
    if not source.exists():
        with tarfile.open(archive) as tar:
            for member in tar.getmembers():
                target = (work / member.name).resolve()
                if work.resolve() not in target.parents or member.issym() or member.islnk():
                    raise SystemExit('Unsafe source archive entry')
            tar.extractall(work)
    build = prefix / 'build'
    command = [args.cmake, '-S', str(source), '-B', str(build), '-DCMAKE_BUILD_TYPE=Release',
               '-DBUILD_TESTING=OFF', '-DCMAKE_POSITION_INDEPENDENT_CODE=ON']
    if args.toolchain:
        tool = args.toolchain.resolve() / 'bin'
        command += ['-DCMAKE_SYSTEM_NAME=Windows', '-DCMAKE_SYSTEM_PROCESSOR=x86_64',
                    '-DOPENSSL_NO_ASM=ON',
                    '-DCMAKE_C_COMPILER=' + str(tool / 'x86_64-w64-mingw32-clang'),
                    '-DCMAKE_CXX_COMPILER=' + str(tool / 'x86_64-w64-mingw32-clang++'),
                    '-DCMAKE_RC_COMPILER=' + str(tool / 'x86_64-w64-mingw32-windres'),
                    '-DCMAKE_CXX_FLAGS=-static']
    elif platform.system() == 'Darwin':
        command += ['-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0']
    elif platform.system() == 'Windows':
        command += ['-A', 'x64', '-DOPENSSL_NO_ASM=ON', '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded']
    subprocess.run(command, check=True)
    subprocess.run([args.cmake, '--build', str(build), '--config', 'Release', '--target', 'crypto', '-j', str(args.jobs)], check=True)
    (prefix / 'lib').mkdir(exist_ok=True)
    libraries = [build / 'libcrypto.a', build / 'Release/crypto.lib', build / 'crypto.lib']
    library = next((p for p in libraries if p.is_file()), None)
    if library is None:
        raise SystemExit('Built BoringSSL crypto library was not found')
    shutil.copy2(library, prefix / 'lib' / library.name)
    shutil.copytree(source / 'include', prefix / 'include', dirs_exist_ok=True)
    shutil.copy2(source / 'LICENSE', prefix / 'LICENSE')
    (prefix / 'SOURCE.json').write_text(json.dumps({'repository':'https://github.com/google/boringssl',
        'commit':COMMIT, 'archive_sha256':SHA256, 'primitive':'BoringSSL SPAKE2 (not RFC 9382 wire-compatible)'}, indent=2)+'\n')
    print(f'BoringSSL crypto ready: {prefix}')

if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Sign inside-out with a persistent Developer ID when configured.

SAN_AUDISCO_SIGNING_IDENTITY is a Keychain identity name or SHA-1, never a key.
Without it these remain explicitly ad-hoc, private development builds.
"""
import argparse
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sign(path, identifier=None, audio=False):
    identity = os.environ.get('SAN_AUDISCO_SIGNING_IDENTITY', '-').strip()
    if not identity:
        raise ValueError('Signing identity must not be empty')
    args = ['/usr/bin/codesign', '--force', '--sign', identity]
    if identifier:
        args += ['--identifier', identifier]
    if identity != '-':
        args += ['--options', 'runtime', '--timestamp']
        if audio:
            args += ['--entitlements', str(ROOT/'installer/macos/audio.entitlements')]
    else:
        args += ['--timestamp=none']
    subprocess.run(args + [str(path)], check=True)


def sign_app(app):
    for binary, identifier in [('bridge-engine', 'local.audiobridge.engine'),
                               ('bridge-agent', 'local.audiobridge.agent')]:
        sign(app/'Contents/MacOS'/binary, identifier, audio=True)
    sign(app, audio=True)
    subprocess.run(['/usr/bin/codesign', '--verify', '--deep', '--strict', str(app)], check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=Path)
    args = parser.parse_args()
    sign_app(args.app)

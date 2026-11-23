#!/usr/bin/env python3
"""Read the release version and monotonic Mac build number from CMake."""
import argparse
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def release_metadata(root=ROOT):
    source = (root / 'CMakeLists.txt').read_text()
    version = re.search(r'project\(SanAudisco\s+VERSION\s+(\d+\.\d+\.\d+)\b', source)
    build = re.search(r'set\(SAN_AUDISCO_MAC_BUILD\s+"?(\d+)"?\s*\)', source)
    if not version or not build:
        raise ValueError('CMake must declare the SanAudisco version and SAN_AUDISCO_MAC_BUILD')
    return version.group(1), build.group(1)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('field', choices=('version', 'mac-build'))
    args = parser.parse_args()
    print(release_metadata()[0 if args.field == 'version' else 1])

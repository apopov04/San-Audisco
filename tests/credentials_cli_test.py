#!/usr/bin/env python3
"""CLI credential parsing and precedence. --store-roundtrip enables scoped OS writes.
Use runtime_fake so a valid run configuration opens deterministic test audio only.
"""
import os
from pathlib import Path
import re
import secrets
import stat
import subprocess
import sys
import tempfile


def main():
    executable = str(Path(sys.argv[1]).resolve())
    live_store = '--store-roundtrip' in sys.argv[2:]
    with tempfile.TemporaryDirectory(prefix='bridge-credential-cli-') as tmp:
        root = Path(tmp)
        key = root / 'pairing ü.key'
        def invoke(*args, ok=True, data=None):
            result = subprocess.run([executable, *map(str, args)], input=data, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10)
            assert (result.returncode == 0) == ok, f'Unexpected status {result.returncode}: {result.stderr}'
            if key.exists():
                secret = key.read_text().strip()
                assert not secret or secret not in result.stdout + result.stderr, 'Secret appeared in command output'
            return result
        invoke('keygen', key)
        original = key.read_bytes()
        assert re.fullmatch(b'[0-9a-f]{64}\n', original), 'Generated file format invalid'
        if os.name == 'posix':
            assert stat.S_IMODE(key.stat().st_mode) == 0o600, 'Generated key permissions are not owner-only'
        invoke('keygen', key, ok=False)
        assert key.read_bytes() == original, 'Existing key was overwritten'
        # These commands fail before any OS protected-store access.
        invoke('key-import', '../escape', key, ok=False)
        invoke('key-delete', '../escape', ok=False)
        malformed = root / 'invalid.key'
        for content in (b'', b'0'*63, b'0'*65, b'0'*64+b' extra', b'g'*64, b'0'*129, b'0'*64+b'\x00'):
            malformed.write_bytes(content)
            invoke('key-import', 'test-malformed', malformed, ok=False)
        config = root / 'run.conf'
        base = f'slot=0\npeer=127.0.0.1\nport=0\npeer_port=45670\nplayback_input=win-playback\nphysical_microphone=win-mic\nheadphones_output=win-headphones\nmicrophone_output=win-mic-sink\n'
        config.write_text(base + f'key_file={key}\nkey_id=../invalid\n')
        result = invoke('run', config, ok=False, data='quit\n')
        assert 'Credential ID' in result.stderr, 'key_id did not take precedence over legacy key_file'
        # Malformed legacy keys use the same bounded parser.
        config.write_text(base + f'key_file={malformed}\n')
        result = invoke('run', config, ok=False, data='quit\n')
        assert 'Pairing key file' in result.stderr, 'Legacy file bypassed bounded parser'
        print('PASS CLI key generation, overwrite protection, invalid IDs, bounded parsing, no secret output and key_id precedence')
        if live_store:
            import socket
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
                s.bind(('127.0.0.1', 0))
                port = s.getsockname()[1]
            credential_id = 'cli-test-' + secrets.token_hex(12)
            created = False
            try:
                invoke('key-import', credential_id, key)
                created = True
                assert key.read_bytes() == original, 'Import unexpectedly altered transfer file'
                config.write_text(base.replace('port=0\n', f'port={port}\n') + f'key_id={credential_id}\nkey_file={root / "missing-transfer-file"}\n')
                invoke('run', config, data='quit\n')
                invoke('key-delete', credential_id)
                created = False
                invoke('run', config, ok=False, data='quit\n')
                print('PASS CLI protected-store import, run with key_id, deletion, missing-key rejection; temporary credential removed')
            finally:
                if created:
                    invoke('key-delete', credential_id)


if __name__ == '__main__':
    main()

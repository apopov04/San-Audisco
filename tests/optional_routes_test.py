#!/usr/bin/env python3
"""Real CLI regression: desktop without mic plus secondary Mac without headphones."""
import json
from pathlib import Path
import secrets
import sys
import tempfile
import time
from runtime_test import EngineControlDriver, Peer, free_port, metric_check, wait_for


def main():
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix='bridge-optional-routes-') as tmp:
        root = Path(tmp)
        key = root / 'pair.key'
        key.write_text(secrets.token_hex(32) + '\n')
        key.chmod(0o600)
        ports = [free_port(), free_port()]
        while ports[0] == ports[1]:
            ports[1] = free_port()
        peers = []
        try:
            for i, role in enumerate(('windows', 'mac')):
                prefix = 'win' if i == 0 else 'mac'
                config = root / f'{prefix}.conf'
                config.write_text(f'slot={i}\nkey_file={key}\npeer=127.0.0.1\nport={ports[i]}\npeer_port={ports[1-i]}\nplayback_input={prefix}-playback\nmicrophone_output={prefix}-mic-sink\n' +
                    ('headphones_output=win-headphones\n' if i == 0 else 'physical_microphone=mac-mic\n'))
                peers.append(Peer(executable, config, root / f'{prefix}.jsonl'))
            win, mac = peers
            controller = EngineControlDriver(peers)
            wait_for(lambda: all(p.latest().get('connected') and p.latest().get('synchronized') for p in peers),
                     'Missing unused devices prevented startup/pairing')
            now = time.time()
            win.command('mute 0')
            wait_for(lambda: metric_check(win, 'win-headphones', now, present=(0, 1))
                     and metric_check(win, 'win-mic-sink', now, present=(3,), absent=(0, 1, 2))
                     and metric_check(mac, 'mac-mic-sink', now, present=(3,), absent=(0, 1, 2)),
                     'Default Windows headphones + Mac microphone route failed')
            print('PASS unused Windows physical microphone and Mac headphones may be omitted', flush=True)
            now = time.time()
            win.command('mic_source 0')
            wait_for(lambda: any('No physical_microphone configured' in e for e in win.errors),
                     'Absent active microphone lacks clear diagnostic')
            # Explicit unmute must not cause fallback to the old or default microphone.
            win.command('mute 0')
            wait_for(lambda: metric_check(win, 'win-mic-sink', now, silent=True)
                     and metric_check(mac, 'mac-mic-sink', now, silent=True),
                     'Missing selected microphone leaked fallback voice')
            print('PASS selecting absent microphone stays silent with diagnostic', flush=True)
            win.command('mic_source 1')
            wait_for(lambda: win.latest().get('mic_source') == 1 and mac.latest().get('mic_source') == 1,
                     'Cannot restore configured microphone')
            now = time.time()
            win.command('mute 0')
            wait_for(lambda: metric_check(win, 'win-mic-sink', now, present=(3,))
                     and metric_check(mac, 'mac-mic-sink', now, present=(3,)),
                     'Configured microphone failed to recover')
            now = time.time()
            mac.command('primary 1')
            wait_for(lambda: any('No headphones_output configured' in e for e in mac.errors),
                     'Absent active headphones lack clear diagnostic')
            mac.command('mute 0')
            wait_for(lambda: metric_check(win, 'win-mic-sink', now, silent=True)
                     and metric_check(mac, 'mac-mic-sink', now, silent=True),
                     'Missing selected headphones did not block microphone route')
            assert not mac.metrics('mac-headphones', now), 'Missing headphones silently used default output'
            print('PASS selecting absent headphones stays silent with diagnostic', flush=True)
            mac.command('primary 0')
            wait_for(lambda: all(p.latest().get('primary') == 0 for p in peers), 'Cannot restore configured primary')
            now = time.time()
            win.command('mute 0')
            wait_for(lambda: metric_check(win, 'win-headphones', now, present=(0, 1))
                     and metric_check(win, 'win-mic-sink', now, present=(3,))
                     and metric_check(mac, 'mac-mic-sink', now, present=(3,)),
                     'Returning to configured primary failed')
            print('PASS configured source and primary recover after unavailable-role selection', flush=True)
            for p in peers:
                p.stop()
                assert p.process.returncode == 0, p.errors
        except Exception:
            for p in peers:
                print('DIAGNOSTIC', p.config.name, p.latest(), p.errors[-8:], file=sys.stderr)
                if p.log.exists():
                    print('\n'.join(p.log.read_text().splitlines()[-6:]), file=sys.stderr)
            raise
        finally:
            for p in peers:
                p.stop(crash=True)


if __name__ == '__main__':
    main()

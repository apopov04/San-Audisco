#!/usr/bin/env python3
"""Measure the actual paired CLI processes with synthetic audio devices.

Includes capture/render callbacks, adaptive queues, mixing, Opus, authenticated
UDP, control synchronization, and synthetic tone generation. Disables only the
fake backend's expensive tone-analysis logger. Excludes native audio drivers,
Bluetooth, Discord, and the game. CPU totals cover complete process lifetimes,
including startup/shutdown; they are not a claim about Windows hardware.
"""
import argparse
import json
import os
from pathlib import Path
import secrets
import socket
import sys
import tempfile
import time
from runtime_test import EngineControlDriver, Peer, wait_for, free_port


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable')
    parser.add_argument('--seconds', type=float, default=20)
    parser.add_argument('--primary', choices=('windows', 'mac'), default='windows')
    parser.add_argument('--output')
    args = parser.parse_args()
    if not hasattr(os, 'wait4'):
        raise SystemExit('Benchmark requires POSIX wait4 for per-process CPU accounting')
    if args.seconds < 5:
        raise SystemExit('Use at least five seconds')
    os.environ['BRIDGE_FAKE_METRICS'] = '0'
    peers = []
    starts = []
    results = []
    with tempfile.TemporaryDirectory(prefix='audio-runtime-benchmark-') as tmp:
        root = Path(tmp)
        key = root/'pair.key'
        key.write_text(secrets.token_hex(32)+'\n')
        key.chmod(0o600)
        ports = [free_port(), free_port()]
        while ports[0] == ports[1]:
            ports[1] = free_port()
        try:
            for i, role in enumerate(('windows', 'mac')):
                prefix = 'win' if i == 0 else 'mac'
                config = root/f'{prefix}.conf'
                config.write_text(f'slot={i}\nkey_file={key}\npeer=127.0.0.1\nport={ports[i]}\npeer_port={ports[1-i]}\nplayback_input={prefix}-playback\nphysical_microphone={prefix}-mic\nheadphones_output={prefix}-headphones\nmicrophone_output={prefix}-mic-sink\n')
                starts.append(time.monotonic())
                peers.append(Peer(str(Path(args.executable).resolve()), config, root/f'{prefix}.jsonl'))
            controller = EngineControlDriver(peers)
            wait_for(lambda: all(p.latest().get('connected') and p.latest().get('synchronized') for p in peers), 'pairing failed')
            desired = int(args.primary == 'mac')
            if desired:
                peers[0].command('primary 1')
                wait_for(lambda: all(p.latest().get('primary') == desired for p in peers), 'primary switch failed')
            peers[0].command('mute 0')
            wait_for(lambda: all(not p.latest().get('muted', True) for p in peers), 'microphone remained muted')
            window_start = time.monotonic()
            while time.monotonic() - window_start < args.seconds:
                if any(p.process.poll() is not None for p in peers):
                    raise AssertionError('Runtime exited during measurement')
                time.sleep(.1)
            states = [p.latest().copy() for p in peers]
            for p in peers:
                p.command('quit')
            for i, p in enumerate(peers):
                _, status, usage = os.wait4(p.process.pid, 0)
                p.process.returncode = os.waitstatus_to_exitcode(status)
                if p.process.returncode:
                    raise AssertionError(f'Nonzero exit: {p.errors}')
                elapsed = time.monotonic()-starts[i]
                cpu = usage.ru_utime+usage.ru_stime
                results.append({
                    'role': ('windows', 'mac')[i], 'host_runtime': sys.platform,
                    'lifetime_wall_seconds': elapsed, 'user_cpu_seconds': usage.ru_utime,
                    'system_cpu_seconds': usage.ru_stime,
                    'percent_of_one_core': cpu/elapsed*100,
                    'peak_rss_mib': usage.ru_maxrss/(1024**2 if sys.platform == 'darwin' else 1024),
                    'last_state': {k:v for k,v in states[i].items() if k != '_received'},
                    'stderr': p.errors,
                })
            report = {
                'scope': 'Actual two-process CLI on this host with synthetic240-frame audio callbacks; native OS audio drivers/Bluetooth/game excluded; tone-analysis instrumentation disabled',
                'primary': args.primary, 'steady_measurement_seconds': args.seconds,
                'cpu_accounting': 'Whole process lifetime, startup and shutdown included', 'processes': results,
            }
            output = json.dumps(report, indent=2)+'\n'
            print(output, end='')
            if args.output:
                Path(args.output).write_text(output)
        finally:
            for p in peers:
                p.stop(crash=True)


if __name__ == '__main__':
    main()

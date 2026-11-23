#!/usr/bin/env python3
"""Private stdin key framing and playback-only test tone, using synthetic audio."""
import json
import math
import os
from pathlib import Path
import secrets
import socket
import statistics
import subprocess
import sys
import tempfile
import threading
import time
from runtime_test import EngineControlDriver


def port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def wait(predicate, label, seconds=12):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        if predicate():
            return
        time.sleep(.025)
    raise AssertionError(label)


class Peer:
    def __init__(self, executable, config, log, initial):
        self.log, self.states, self.errors = log, [], []
        self.controller = None
        self.process = subprocess.Popen([str(executable), 'run', str(config)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            env=dict(os.environ, BRIDGE_FAKE_LOG=str(log), BRIDGE_FAKE_METRICS='1'))

        def output():
            for line in self.process.stdout:
                self.states.append(json.loads(line))

        def errors():
            for line in self.process.stderr:
                self.errors.append(line.decode(errors='replace').strip())

        threading.Thread(target=output, daemon=True).start()
        threading.Thread(target=errors, daemon=True).start()
        # One write deliberately coalesces the secret frame and commands. A
        # buffered getline in bootstrap would steal those commands from poll().
        self.process.stdin.write(initial)
        self.process.stdin.flush()

    def command(self, command):
        if self.controller is not None and command.strip() not in ('quit', 'test_tone') and not command.startswith('state '):
            self.controller.change(command)
            return
        self.raw_command(command)

    def raw_command(self, command):
        self.process.stdin.write(command.encode() + b'\n')
        self.process.stdin.flush()

    def state(self):
        return self.states[-1] if self.states else {}

    def metrics(self, sink, after, before):
        result = []
        if self.log.exists():
            for line in self.log.read_text().splitlines():
                try:
                    value = json.loads(line)
                    if value['sink'] == sink and after <= value['time'] <= before:
                        result.append(value)
                except ValueError:
                    pass
        return result

    def stop(self):
        if self.process.poll() is None:
            try:
                self.command('quit')
                self.process.wait(timeout=5)
            except (BrokenPipeError, subprocess.TimeoutExpired):
                self.process.kill()
                self.process.wait()


def unexplained_rms(samples):
    # The fake backend measures the four source frequencies. Energy outside
    # those tones includes the new 440 Hz diagnostic tone (and codec noise).
    assert len(samples) >= 4, 'Too few synthetic audio measurements'
    return statistics.median(math.sqrt(max(0, m['rms'] ** 2 -
        sum(t ** 2 for t in m['tones']) / 2)) for m in samples)


def main():
    executable = Path(sys.argv[1]).resolve()
    key = secrets.token_hex(32).encode()
    with tempfile.TemporaryDirectory(prefix='san-audisco-bootstrap-') as temp:
        root = Path(temp)
        ports = [port(), port()]
        configs = []
        for index, (platform, prefix) in enumerate((('windows', 'win'), ('mac', 'mac'))):
            config = root / (platform + '.conf')
            config.write_text(f'slot={index}\nkey_stdin=1\npeer=127.0.0.1\n'
                f'port={ports[index]}\npeer_port={ports[1-index]}\n'
                f'playback_input={prefix}-playback\nphysical_microphone={prefix}-mic\n'
                f'headphones_output={prefix}-headphones\nmicrophone_output={prefix}-mic-sink\n')
            configs.append(config)
        invalid = [b'', key[:12], key[:63] + b'\n', key, key + b'X',
                   b'g' * 64 + b'\n', key + b'0\n', b'\x00' * 64 + b'\n']
        for frame in invalid:
            result = subprocess.run([str(executable), 'run', str(configs[0])],
                input=frame, capture_output=True, timeout=5)
            assert result.returncode != 0, 'Malformed private key accepted'
            assert b'media-key' in result.stderr, result.stderr
            assert not result.stdout, 'Engine started before valid bootstrap'
            assert key not in result.stdout + result.stderr, 'Secret leaked in diagnostics'
        print('PASS empty, short, nonhex, NUL, overlong and invalid-delimiter key frames rejected', flush=True)
        peers = []
        try:
            initial = EngineControlDriver([], initial={'slot0_volume': .37}, publish=False)
            frame = key+b'\n'+initial.frame().encode()+b'\n'
            win = Peer(executable, configs[0], root/'win.jsonl', frame)
            peers.append(win)
            mac = Peer(executable, configs[1], root/'mac.jsonl', frame)
            peers.append(mac)
            controller = EngineControlDriver(peers, initial={'slot0_volume': .37}, publish=False)
            wait(lambda: win.state().get('synchronized') and mac.state().get('synchronized'), 'Engine pair did not synchronize')
            wait(lambda: abs(win.state().get('slot0_volume', -1)-.37) < .001 and
                 abs(mac.state().get('slot0_volume', -1)-.37) < .001,
                 'Commands coalesced with key frame were lost')
            print('PASS valid private bootstrap preserves queued control commands', flush=True)
            win.command('slot0_volume 1')
            win.command('mute 0')
            wait(lambda: not win.state().get('muted', True) and not mac.state().get('muted', True), 'Unmute failed')
            sinks = ((win, 'win-headphones'), (win, 'win-mic-sink'), (mac, 'mac-mic-sink'))
            baseline_samples = {}
            baseline_after = time.time()+.2

            def audible_baseline():
                # Unmuted telemetry acknowledges controls, not filled audio
                # reservoirs. Require current, completed PCM windows on BOTH
                # microphone sinks before testing tone isolation.
                now = time.time()
                current = {sink: peer.metrics(sink, max(baseline_after, now-.8), now)[-6:]
                           for peer, sink in sinks}
                if any(len(rows) < 6 for rows in current.values()):
                    return False
                if any(any(row['tones'][3] <= .06 for row in current[sink])
                       for sink in ('win-mic-sink', 'mac-mic-sink')):
                    return False
                baseline_samples.update(current)
                return True

            wait(audible_baseline, 'Both microphone routes did not produce a stable audible baseline')
            tone_start = time.time()
            win.command('test_tone')
            tone_end = tone_start+1.6
            # The logger stamps windows when they finish. Wait for each sink
            # to finish the interval; a fixed sleep is not a logger barrier.
            wait(lambda: all(peer.metrics(sink, tone_end, time.time()) for peer, sink in sinks),
                 'Tone measurement windows did not complete', seconds=5)
            during_samples = {sink: peer.metrics(sink, tone_start+.3, tone_end) for peer, sink in sinks}
            before = unexplained_rms(baseline_samples['win-headphones'])
            during = unexplained_rms(during_samples['win-headphones'])
            assert during > before+.012, ('Diagnostic tone absent from headphones', before, during)
            for _, sink in sinks[1:]:
                prior = unexplained_rms(baseline_samples[sink])
                tone = unexplained_rms(during_samples[sink])
                assert tone < max(.012, prior+.006), ('Diagnostic tone leaked into microphone', sink, prior, tone)
                microphone = statistics.median(m['tones'][3] for m in during_samples[sink])
                assert microphone > .06, ('Isolation tested with inaudible microphone', sink, microphone)
            print('PASS diagnostic tone reaches headphones without entering either unmuted microphone route', flush=True)
        except Exception:
            for peer in peers:
                print('DIAGNOSTIC', peer.log.name, peer.state(), peer.errors[-8:], file=sys.stderr)
                if peer.log.exists():
                    print('\n'.join(peer.log.read_text().splitlines()[-18:]), file=sys.stderr)
            raise
        finally:
            for peer in peers:
                peer.stop()


if __name__ == '__main__':
    main()

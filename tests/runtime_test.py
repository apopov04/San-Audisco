#!/usr/bin/env python3
"""Exercise the real CLI, control protocol, network codec, and audio graph in two processes.
Only the platform AudioStream is replaced with deterministic tone devices.
A test-only control driver delivers agent-authorized snapshots to both engines;
this suite does not claim to validate the room authority/handover protocol.
"""
import json
import os
from pathlib import Path
import secrets
import signal
import socket
import statistics
import subprocess
import sys
import tempfile
import threading
import time


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


class Peer:
    def __init__(self, executable, config, log):
        self.executable, self.config, self.log = executable, config, log
        self.states, self.errors = [], []
        self.process = None
        self.controller = None
        self.start()

    def start(self):
        self.states.clear()
        env = dict(os.environ, BRIDGE_FAKE_LOG=str(self.log))
        self.process = subprocess.Popen([self.executable, 'run', str(self.config)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, bufsize=1, env=env)
        def read_output(process):
            for line in process.stdout:
                try:
                    state = json.loads(line)
                    state['_received'] = time.time()
                    self.states.append(state)
                except ValueError:
                    self.errors.append(line.strip())
        def read_errors(process):
            for line in process.stderr:
                self.errors.append(line.strip())
        threading.Thread(target=read_output, args=(self.process,), daemon=True).start()
        threading.Thread(target=read_errors, args=(self.process,), daemon=True).start()
        if self.controller is not None:
            self.controller.reconnect()

    def command(self, text):
        if self.controller is not None and text.strip() not in ('quit', 'test_tone') and not text.startswith('state '):
            self.controller.change(text)
            return
        self.raw_command(text)

    def raw_command(self, text):
        if self.process.poll() is not None:
            raise AssertionError(f'Peer exited {self.process.returncode}: {self.errors}')
        self.process.stdin.write(text.rstrip() + '\n')
        self.process.stdin.flush()

    def stop(self, crash=False):
        if self.process and self.process.poll() is None:
            if crash:
                self.process.kill()
            else:
                try:
                    self.command('quit')
                except (BrokenPipeError, AssertionError):
                    pass
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
                raise AssertionError('Runtime failed to exit')

    def latest(self):
        return self.states[-1] if self.states else {}

    def metrics(self, sink, after):
        if not self.log.exists():
            return []
        result = []
        for line in self.log.read_text().splitlines():
            try:
                m = json.loads(line)
                if m['sink'] == sink and m['time'] >= after:
                    result.append(m)
            except ValueError:
                pass  # The logger may be appending its final line concurrently.
        return result


class EngineControlDriver:
    """Test-only agent stand-in. No networking or authority logic lives here.

    Production agents negotiate ownership separately; engine tests deliver the
    resulting complete snapshot directly to each child process, over stdin.
    """
    def __init__(self, peers, initial=None, publish=True):
        self.peers = peers
        self.epoch, self.revision = 1, 1
        self.settings = dict(primary=0, mic_source=1, mute=1, deafen=0, balance=0, master=1)
        for slot in (0, 1):
            self.settings.update({f'slot{slot}_volume': 1, f'slot{slot}_mute': 0,
                                  f'slot{slot}_deafen': 0, f'slot{slot}_silence': 0})
        if initial:
            self.settings.update(initial)
        for peer in peers:
            peer.controller = self
        if publish:
            self.publish()

    def frame(self, epoch=None, revision=None):
        snapshot = '|'.join(f'{key} {value}' for key, value in self.settings.items())
        return f'state {self.epoch if epoch is None else epoch} {self.revision if revision is None else revision} {snapshot}'

    def publish(self):
        for peer in self.peers:
            if peer.process.poll() is None:
                peer.raw_command(self.frame())

    def change(self, commands):
        changed_roles = False
        for row in commands.splitlines():
            key, value = row.split()
            assert key in self.settings, f'Unknown test control {key}'
            value = float(value)
            if key in ('primary', 'mic_source') and self.settings[key] != value:
                changed_roles = True
            self.settings[key] = value
        if changed_roles:
            self.epoch += 1
            self.settings['mute'] = 1
        self.revision += 1
        self.publish()

    def reconnect(self):
        self.epoch += 1
        self.revision += 1
        self.settings['mute'] = 1
        self.publish()


def wait_for(predicate, description, timeout=8):
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        if predicate():
            return
        time.sleep(.025)
    raise AssertionError(description)


def metric_check(peer, sink, after, present=(), absent=(), silent=False, levels=None):
    # Inspect several completed windows after all queued pre-command audio drains.
    rows = peer.metrics(sink, after + .35)
    if len(rows) < 3:
        return False
    rows = rows[-3:]
    tones = [statistics.median(m['tones'][i] for m in rows) for i in range(4)]
    rms = statistics.median(m['rms'] for m in rows)
    if silent and rms > .002:
        return False
    if any(tones[i] < .04 for i in present):
        return False
    if any(tones[i] > .008 for i in absent):
        return False
    if levels and any(not lo <= tones[i] <= hi for i, (lo, hi) in levels.items()):
        return False
    return True


def main():
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix='san-audisco-runtime-') as tmp:
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
                config.write_text(f'slot={i}\nkey_file={key}\npeer=127.0.0.1\nport={ports[i]}\npeer_port={ports[1-i]}\nplayback_input={prefix}-playback\nphysical_microphone={prefix}-mic\nheadphones_output={prefix}-headphones\nmicrophone_output={prefix}-mic-sink\n')
                peers.append(Peer(executable, config, root / f'{prefix}.jsonl'))
            win, mac = peers
            controller = EngineControlDriver(peers)
            checks = 0
            def connected():
                return all(p.latest().get('connected') and p.latest().get('synchronized', True) for p in peers)
            def verify(label, *conditions):
                nonlocal checks
                wait_for(lambda: all(f() for f in conditions), label)
                checks += 1
                print('PASS', label, flush=True)
            def step(peer, commands):
                when = time.time()
                peer.command(commands)
                if os.environ.get('BRIDGE_RUNTIME_STRESS') == '1' and commands in (
                        'slot1_deafen 0\nslot0_volume 0.25', 'deafen 0'):
                    if os.name == 'posix' and hasattr(signal, 'SIGSTOP'):
                        # Fixed staggered pauses exercise control propagation and receive
                        # re-priming while output callback queues are being reset. No audio
                        # thresholds or settling windows are relaxed in this mode.
                        for target, duration in ((win, .06), (mac, .09), (win, .04), (mac, .03)):
                            target.process.send_signal(signal.SIGSTOP)
                            try:
                                time.sleep(duration)
                            finally:
                                if target.process.poll() is None:
                                    target.process.send_signal(signal.SIGCONT)
                            time.sleep(.025)
                        print('STRESS fixed staggered peer pauses during control transition', flush=True)
                    else:
                        print('SKIP scheduler stress: SIGSTOP/SIGCONT unavailable', flush=True)
                return when
            def audio(peer, sink, after, **kw):
                return lambda: metric_check(peer, sink, after, **kw)
            wait_for(connected, 'paired runtime did not connect')
            now = time.time()
            verify('initial mixed playback and fail-closed microphone',
                audio(win, 'win-headphones', now, present=(0, 1), absent=(2, 3)),
                audio(win, 'win-mic-sink', now, silent=True), audio(mac, 'mac-mic-sink', now, silent=True))
            now = step(mac, 'mute 0')
            verify('Mac command unmutes same microphone to both applications',
                audio(win, 'win-mic-sink', now, present=(3,), absent=(0, 1, 2)),
                audio(mac, 'mac-mic-sink', now, present=(3,), absent=(0, 1, 2)))
            now = step(mac, 'slot0_mute 1')
            verify('Windows microphone mute leaves Mac microphone audible',
                audio(win, 'win-mic-sink', now, silent=True), audio(mac, 'mac-mic-sink', now, present=(3,)))
            step(win, 'slot0_mute 0')
            now = step(mac, 'slot1_deafen 1')
            verify('Mac deafen gates its playback and its microphone destination',
                audio(win, 'win-headphones', now, present=(0,), absent=(1,)),
                audio(mac, 'mac-mic-sink', now, silent=True), audio(win, 'win-mic-sink', now, present=(3,)))
            now = step(win, 'slot1_deafen 0\nslot0_volume 0.25')
            verify('independent volume slider attenuates Windows source',
                audio(win, 'win-headphones', now, levels={0: (.022, .052), 1: (.09, .19)}))
            now = step(mac, 'slot0_volume 1\nbalance -1')
            verify('balance endpoint selects Windows audio', audio(win, 'win-headphones', now, present=(0,), absent=(1,)))
            now = step(win, 'balance 0\nslot1_silence 1')
            verify('silence affects listening only', audio(win, 'win-headphones', now, present=(0,), absent=(1,)),
                audio(mac, 'mac-mic-sink', now, present=(3,)))
            step(win, 'slot1_silence 0')
            now = step(mac, 'primary 1')
            verify('primary handover moves mixed playback to Mac and blocks voice',
                audio(mac, 'mac-headphones', now, present=(0, 1)),
                audio(win, 'win-mic-sink', now, silent=True), audio(mac, 'mac-mic-sink', now, silent=True))
            now = step(mac, 'mute 0')
            verify('voice can resume after primary handover', audio(win, 'win-mic-sink', now, present=(3,)),
                audio(mac, 'mac-mic-sink', now, present=(3,)))
            now = step(win, 'mic_source 0')
            verify('microphone source handover blocks voice', audio(win, 'win-mic-sink', now, silent=True),
                audio(mac, 'mac-mic-sink', now, silent=True))
            now = step(mac, 'mute 0')
            verify('Windows microphone reaches both applications', audio(win, 'win-mic-sink', now, present=(2,), absent=(0,1,3)),
                audio(mac, 'mac-mic-sink', now, present=(2,), absent=(0,1,3)))
            now = step(mac, 'deafen 1')
            verify('global deafen silences playback and both microphones', audio(mac, 'mac-headphones', now, silent=True),
                audio(win, 'win-mic-sink', now, silent=True), audio(mac, 'mac-mic-sink', now, silent=True))
            now = step(win, 'deafen 0')
            verify('undeafen restores prior route state', audio(mac, 'mac-headphones', now, present=(0,1)),
                audio(win, 'win-mic-sink', now, present=(2,)), audio(mac, 'mac-mic-sink', now, present=(2,)))
            # A delayed previous authority or old revision must not overwrite the
            # current acknowledged mix. Exercise the actual engine parser and audio,
            # not only the test driver's state bookkeeping.
            now = time.time()
            muted_mix = '|'.join(f'{key} {0 if key == "master" else value}'
                                 for key, value in controller.settings.items())
            for peer in peers:
                peer.raw_command(f'state {controller.epoch-1} {controller.revision+1000} {muted_mix}')
                peer.raw_command(f'state {controller.epoch} {controller.revision-1} {muted_mix}')
            verify('stale authority and settings revisions cannot replace the audible mix',
                audio(mac, 'mac-headphones', now, present=(0,1)),
                audio(win, 'win-mic-sink', now, present=(2,)),
                audio(mac, 'mac-mic-sink', now, present=(2,)))
            wait_for(lambda: all(p.latest().get('_received', 0) > now and
                                 p.latest().get('authority_epoch') == controller.epoch and
                                 p.latest().get('settings_revision') == controller.revision for p in peers),
                     'stale state changed engine authority or revision')
            # A short scheduler suspension must not leave incoming timestamps permanently
            # behind playout. It is shorter than the transport liveness timeout.
            if os.name == 'posix' and hasattr(signal, 'SIGSTOP'):
                win.process.send_signal(signal.SIGSTOP)
                try:
                    time.sleep(.3)
                finally:
                    win.process.send_signal(signal.SIGCONT)
                resumed = time.time()
                wait_for(lambda: metric_check(mac, 'mac-headphones', resumed, present=(0, 1))
                         and metric_check(mac, 'mac-mic-sink', resumed, present=(2,), absent=(0, 1, 3)),
                         '300ms scheduler suspension permanently stalled remote playback/microphone', timeout=2)
                checks += 1
                print('PASS 300ms scheduler suspension recovers remote playback and microphone within 2s', flush=True)
            else:
                print('SKIP scheduler suspension: SIGSTOP/SIGCONT unavailable on this platform', flush=True)
            mac.stop(crash=True)
            now = time.time()
            wait_for(lambda: not win.latest().get('connected', True), 'peer crash did not expire transport')
            verify('peer crash fails microphone closed', audio(win, 'win-mic-sink', now + 2, silent=True))
            mac.start()
            wait_for(connected, 'restarted peer failed to reconnect', timeout=12)
            now = time.time()
            verify('reconnect preserves mute until explicit unmute', audio(win, 'win-mic-sink', now, silent=True),
                audio(mac, 'mac-mic-sink', now, silent=True))
            now = step(mac, 'mute 0')
            verify('restarted peer command and microphone route recover', audio(win, 'win-mic-sink', now, present=(2,)),
                audio(mac, 'mac-mic-sink', now, present=(2,)))
            for p in peers:
                p.stop()
                if p.process.returncode != 0:
                    raise AssertionError(f'Runtime exit {p.process.returncode}: {p.errors}')
            print(f'PASS actual CLI runtime: {checks} scenarios, two processes, encrypted Opus transport, fake device callbacks')
        except Exception:
            for p in peers:
                print('DIAGNOSTIC', p.config.name, 'state', p.latest(), 'stderr', p.errors[-10:], file=sys.stderr)
                if p.log.exists():
                    print('\n'.join(p.log.read_text().splitlines()[-8:]), file=sys.stderr)
            raise
        finally:
            for p in peers:
                p.stop(crash=True)


if __name__ == '__main__':
    main()

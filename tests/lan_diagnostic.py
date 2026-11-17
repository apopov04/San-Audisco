#!/usr/bin/env python3
"""Two-host synthetic test. Windows controls, Mac observes replicated settings.

No physical audio devices are used. Both hosts retain local state/metrics logs.
Windows holds each phase for at least four seconds and waits for the Mac
assessment acknowledgement. Stale audio before the local state transition is
excluded. See LAN_TEST.md for setup and the limits of sampled telemetry.
"""
import argparse
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parent))
from runtime_test import Peer, metric_check, wait_for

BASE = dict(primary=0, mic_source=1, muted=True, deafen=False, balance=0,
            master=1, win_mute=False, mac_mute=False, win_deafen=False,
            mac_deafen=False, win_volume=1, mac_volume=1,
            win_silence=False, mac_silence=False)

def build_phases():
    state = BASE.copy()
    phases = []
    def add(name, commands='', **changes):
        state.update(changes)
        phases.append(dict(name=name, commands=commands, state=state.copy()))
    add('initial_mixed_playback_and_muted_voice')
    add('mac_microphone_to_both', 'mute 0', muted=False)
    add('windows_route_mute', 'win_mute 1', win_mute=True)
    add('mac_route_mute', 'win_mute 0\nmac_mute 1', win_mute=False, mac_mute=True)
    add('mac_route_deafen', 'mac_mute 0\nmac_deafen 1', mac_mute=False, mac_deafen=True)
    add('windows_route_deafen', 'mac_deafen 0\nwin_deafen 1', mac_deafen=False, win_deafen=True)
    add('windows_volume_quarter', 'win_deafen 0\nwin_volume 0.25', win_deafen=False, win_volume=.25)
    add('balance_windows_only', 'win_volume 1\nbalance -1', win_volume=1, balance=-1)
    add('mac_listening_silence', 'balance 0\nmac_silence 1', balance=0, mac_silence=True)
    add('primary_mac_handover_muted', 'mac_silence 0\nprimary 1', mac_silence=False, primary=1, muted=True)
    add('voice_resumes_after_handover', 'mute 0', muted=False)
    add('windows_microphone_handover_muted', 'mic_source 0', mic_source=0, muted=True)
    add('windows_microphone_to_both', 'mute 0', muted=False)
    add('global_deafen', 'deafen 1', deafen=True)
    add('global_undeafen', 'deafen 0', deafen=False)
    add('global_mute', 'mute 1', muted=True)
    return phases

def matches(actual, expected):
    return actual.get('connected') and actual.get('synchronized') and all(
        k in actual and abs(float(actual[k])-float(v)) < .001
        for k, v in expected.items())

def check_audio(peer, role, expected, after):
    prefix = 'win' if role == 'windows' else 'mac'
    voice_blocked = expected['muted'] or expected['deafen'] or expected[prefix+'_mute'] or expected[prefix+'_deafen']
    checks = []
    if voice_blocked:
        checks.append(metric_check(peer, prefix+'-mic-sink', after, silent=True))
    else:
        tone = 2 + expected['mic_source']
        checks.append(metric_check(peer, prefix+'-mic-sink', after,
                                  present=(tone,), absent=tuple(i for i in range(4) if i != tone)))
    local_primary = expected['primary'] == int(role == 'mac')
    if not local_primary:
        checks.append(peer.latest().get('headphones_ready') is False)
    elif expected['deafen']:
        checks.append(metric_check(peer, prefix+'-headphones', after, silent=True))
    else:
        present, absent, levels = [], [2, 3], {}
        for index, source in enumerate(('win', 'mac')):
            blocked = expected[source+'_deafen'] or expected[source+'_silence']
            blocked = blocked or (expected['balance'] == -1 and index == 1)
            if blocked:
                absent.append(index)
            elif expected[source+'_volume'] == .25:
                levels[index] = (.022, .052)
            else:
                present.append(index)
        checks.append(metric_check(peer, prefix+'-headphones', after,
                                  present=tuple(present), absent=tuple(absent), levels=levels))
    return all(checks)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('role', choices=('windows', 'mac'))
    parser.add_argument('executable')
    parser.add_argument('--peer', required=True)
    parser.add_argument('--port', type=int, default=45672)
    parser.add_argument('--key', required=True, help='Explicit path to the same disposable pairing-key file on both hosts')
    parser.add_argument('--output', required=True)
    parser.add_argument('--wait-seconds', type=float, default=600)
    args = parser.parse_args()
    root = Path(args.output).resolve()
    root.mkdir(parents=True, exist_ok=False)
    prefix = 'win' if args.role == 'windows' else 'mac'
    config = root / 'peer.conf'
    config.write_text(f'device={args.role}\npeer={args.peer}\nport={args.port}\npeer_port={args.port}\nkey_file={Path(args.key).resolve()}\nplayback_input={prefix}-playback\nphysical_microphone={prefix}-mic\nheadphones_output={prefix}-headphones\nmicrophone_output={prefix}-mic-sink\n')
    peer = Peer(str(Path(args.executable).resolve()), config, root/'audio.jsonl')
    results = []
    started = time.time()
    failure = None
    try:
        print('WAITING for authenticated peer; only synthetic audio is used', flush=True)
        wait_for(lambda: peer.latest().get('connected') and peer.latest().get('synchronized'),
                 'LAN pairing failed (check peer IP, UDP port and firewall)', timeout=args.wait_seconds)
        print('CONNECTED', flush=True)
        phases = build_phases()
        for index, phase in enumerate(phases):
            if args.role == 'windows':
                # Reset the prior ACK and change controls in one worker read.
                peer.command('master 1' + ('\n'+phase['commands'] if phase['commands'] else ''))
            wait_for(lambda: matches(peer.latest(), phase['state']),
                     'Replicated state missing for '+phase['name'], timeout=15)
            observed = time.time()
            bad = []
            checks = 0
            # Keep assessing after the first good sample. This catches a
            # disconnect later in the held phase instead of reporting early PASS.
            until = time.monotonic() + 3.0
            while time.monotonic() < until:
                actual = peer.latest()
                state_expected = phase['state'].copy()
                if args.role == 'windows' and actual.get('master') in (.98, .99):
                    state_expected['master'] = actual['master']
                if not matches(actual, state_expected):
                    bad.append(dict(at=time.time(), reason='state_or_connection_changed', state=actual.copy()))
                # Existing metric thresholds and 350ms queue-drain exclusion.
                # Allow 0.8 seconds to collect three complete 100ms windows.
                if time.time()-observed >= .8:
                    checks += 1
                    if not check_audio(peer, args.role, phase['state'], observed):
                        bad.append(dict(at=time.time(), reason='audio_threshold'))
                time.sleep(.05)
            passed = not bad and checks > 0
            peer_ack = None
            if args.role == 'mac':
                peer.command('master '+('0.99' if passed else '0.98'))
                # After assessment, keep watching connection health until the
                # controller moves to the next phase. Audio can transition
                # before one-second telemetry, so do not grade old audio here.
                boundary_deadline=time.monotonic()+15
                while time.monotonic()<boundary_deadline:
                    actual=peer.latest()
                    advanced=(matches(actual,phases[index+1]['state']) if index+1<len(phases) else actual.get('master')==.97)
                    if advanced:break
                    if not actual.get('connected') or not actual.get('synchronized'):
                        bad.append(dict(at=time.time(),reason='connection_changed_during_hold',state=actual.copy()))
                    time.sleep(.05)
                else:
                    bad.append(dict(at=time.time(),reason='controller_phase_boundary_timeout'))
                passed=passed and not bad
            else:
                ack_deadline = time.monotonic()+15
                while time.monotonic()<ack_deadline:
                    actual=peer.latest()
                    if actual.get('master') in (.98,.99):
                        peer_ack=actual['master']
                        break
                    if not matches(actual,phase['state']):
                        bad.append(dict(at=time.time(),reason='state_or_connection_while_waiting_ack',state=actual.copy()))
                    if not check_audio(peer,args.role,phase['state'],observed):
                        bad.append(dict(at=time.time(),reason='audio_while_waiting_ack'))
                    time.sleep(.05)
                if peer_ack is None:
                    bad.append(dict(at=time.time(),reason='mac_ack_timeout'))
                elif peer_ack == .98:
                    bad.append(dict(at=time.time(),reason='mac_reported_phase_failure'))
                passed=passed and not bad
                # Hold for >=4 seconds and continue checking, including after ACK.
                while time.time()-observed<4:
                    actual=peer.latest(); expected=phase['state'].copy()
                    if actual.get('master') in (.98,.99):expected['master']=actual['master']
                    if not matches(actual,expected) or not check_audio(peer,args.role,phase['state'],observed):
                        bad.append(dict(at=time.time(),reason='held_phase_health_failure',state=actual.copy()))
                    time.sleep(.05)
                passed=passed and not bad
            result=dict(phase=phase['name'],passed=passed,observed_at=observed,completed_at=time.time(),
                        checks=checks,peer_ack=peer_ack,failures=bad,last_state=peer.latest().copy())
            results.append(result)
            print(('PASS ' if passed else 'FAIL ')+phase['name'],flush=True)
        if args.role=='windows':
            peer.command('master 0.97')
            wait_for(lambda: peer.latest().get('master')==.97,'final marker missing',timeout=5)
            time.sleep(1)
    except Exception as exc:
        failure = str(exc)
        print('FAIL '+failure, file=sys.stderr, flush=True)
    finally:
        peer.stop()
        (root/'states.jsonl').write_text(''.join(json.dumps(s)+'\n' for s in peer.states))
        report = dict(role=args.role, test_frequencies_hz=[410,730,1090,2390], network_reservoir_frames=4800, network_reservoir_ms=100, jitter_packet_capacity=32, jitter_prefill_packets=4, hardware_buffer_frames=1536, local_listening_target='callback-derived; unchanged', windows_pacing='high_resolution_waitable_timer', mac_pacing='std_sleep_until', microphone_bitrate=48000, thresholds=dict(present=.04,absent=.008,silent_rms=.002), scope='Cross-host production runtime_fake CLI, codec, authenticated transport, control and mixing with synthetic audio callbacks; excludes physical devices, drivers, Bluetooth, Discord and game',
                      started_at=started, ended_at=time.time(), results=results,
                      failure=failure, stderr=peer.errors, exit_code=peer.process.returncode)
        (root/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    return 0 if failure is None and len(results)==16 and all(r['passed'] for r in results) and peer.process.returncode==0 else 1

if __name__ == '__main__':
    raise SystemExit(main())

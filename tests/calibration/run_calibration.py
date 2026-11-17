#!/usr/bin/env python3
"""Explicit, offline calibration of routing-test tones against real libopus.

Compiles and runs the complete 480-case matrix. Does not install anything,
access audio devices, create network sockets, or change production sources.
Use a fresh output directory to preserve previous evidence.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--opus-prefix', required=True,
                        help='Prefix containing include/opus/opus.h and lib/libopus')
    parser.add_argument('--compiler', default='c++')
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    prefix = Path(args.opus_prefix).resolve()
    source = Path(__file__).with_name('codec_tone_calibration.cpp')
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    executable = output / ('calibrate.exe' if sys.platform == 'win32' else 'calibrate')
    command = [args.compiler, '-std=c++20', '-O2', '-I'+str(prefix/'include'),
               str(source), '-L'+str(prefix/'lib'), '-lopus', '-o', str(executable)]
    (output/'build-command.json').write_text(json.dumps(command, indent=2)+'\n')
    subprocess.run(command, check=True, timeout=120)
    with (output/'results.jsonl').open('w') as stream:
        subprocess.run([str(executable)], stdout=stream, check=True, timeout=180)
    rows = [json.loads(line) for line in (output/'results.jsonl').read_text().splitlines()]
    coverage = {(r['source'], r['contamination'], r['drift_mode'], r['initial_phase']) for r in rows}
    expected = {(source_index, contamination, drift, initial)
                for source_index in range(4)
                for contamination in range(-1, 4) if contamination != source_index
                for drift in range(10) for initial in (0., .7, 1.9)}
    assert len(rows) == 480 and coverage == expected, 'Incomplete or duplicated matrix'
    false_positives = sum(r['false_positive_windows'] for r in rows)
    missed = sum(r['missed_contamination_windows'] for r in rows)
    minimum_wanted = min(r['min'][r['source']] for r in rows)
    maximum_spurious = max(r['max'][i] for r in rows for i in range(4)
                          if i not in (r['source'], r['contamination']))
    minimum_injected = min(r['min'][r['contamination']] for r in rows
                           if r['contamination'] >= 0)
    passed = false_positives == 0 and missed == 0 and minimum_wanted >= .04
    summary = {
        'passed': passed,
        'scope': 'Offline real-Opus tone calibration; no network, queues, PLC or physical audio',
        'frequencies_hz': {'windows_playback': 410, 'mac_playback': 730,
                           'windows_microphone': 1090, 'mac_microphone': 2390},
        'codec': {'sample_rate': 48000, 'frame_samples': 240, 'frame_ms': 5,
                  'pcm_channels': 2, 'application': 'restricted_lowdelay',
                  'complexity': 3, 'vbr': False, 'force_channels': 'AUTO unchanged',
                  'playback_bitrate': 128000, 'microphone_bitrate': 48000},
        'thresholds': {'present_min': .04, 'absent_max': .008, 'silence_rms_max': .002},
        'cases': len(rows), 'clean_cases': 120, 'contamination_cases': 360,
        'three_window_assessments_per_case': 89,
        'drift': 'Nine fixed offsets -1000..+1000 ppm in 250 ppm steps, plus a triangle slewing 10 ppm per 5 ms block',
        'initial_phases_radians': [0, .7, 1.9],
        'signals': 'Wanted amplitude 0.15; each wrong-source negative control adds amplitude 0.015 before encoding',
        'false_positive_windows': false_positives,
        'missed_contamination_windows': missed,
        'minimum_wanted_amplitude': minimum_wanted,
        'maximum_spurious_amplitude': maximum_spurious,
        'minimum_injected_amplitude': minimum_injected,
        'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'executable_sha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
        'results_sha256': hashlib.sha256((output/'results.jsonl').read_bytes()).hexdigest(),
        'platform': platform.platform(),
    }
    (output/'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    shutil.copy2(source, output/source.name)
    print(json.dumps(summary, indent=2))
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())

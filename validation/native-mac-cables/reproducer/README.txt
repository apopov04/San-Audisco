Archived reproducer. Copy these three files back to work/native-cable-integration
in the original workspace layout before using the commands below. The runner
uses that relative layout and overwrites its local measurement files; preserve
the archived evidence and run only from a separate scratch copy.

Native Mac virtual-cable integration evidence

Result: 12/12 scenarios PASS in 9.88 seconds, CLI exit codes0/0, helper exit0.
Disposable pairing key and config directory removed. No production code changed.

Reproduce from workspace /Users/apopov/Documents/Codex/2026-11-17/i-ha:
/usr/bin/clang++ -std=c++20 -O2 -mmacosx-version-min=13.0 -Ioutputs/AudioBridge/include work/native-cable-integration/cable_helper.cpp outputs/AudioBridge/src/audio_mac.cpp -framework AudioUnit -framework CoreAudio -framework CoreFoundation -o work/native-cable-integration/cable_helper
python3 work/native-cable-integration/run.py > work/native-cable-integration/suite.log 2>&1

Requires already installed/active BlackHole2ch_UID and BlackHole16ch_UID.
Both enumerated at48kHz during this run. Helper output is calibrated730Hz,
peak0.15, stereo48kHz into BlackHole2ch. Production Mac CLI captures that cable,
sends encoded/encrypted playback over localhost UDP to a runtime_fake Windows
role (which generates410Hz playback and1090Hz microphone). Fake Windows primary
mixes both playback sources. The same Windows microphone is fed to the fake
Windows mic sink and sent back to the production Mac CLI, which renders into
BlackHole16ch. Native helper captures both stereo channels of that cable.

Only virtual-cable AudioStreams are opened by the helper and production Mac
CLI. No physical microphones or speakers are opened; system defaults are never
written. The test neither alters installed drivers nor changes nominal rates.
The Mac config intentionally omits physical_microphone and headphones_output.
It remains a secondary playback device, while Windows is primary and mic source.
The initial default Mac mic source produces expected unavailable-mic diagnostics
until the test selects Windows; startup remains muted. Other stderr is empty.

The helper has a120-second safety limit, signal-aware orderly stop, callback
queue overflow checks, and an allocation-free callback measurement queue. It
records100ms tone/RMS windows independently for left and right channels. The
harness uses existing runtime_test thresholds: after350ms settling, median of
latest3 windows; present tone>0.04, absent tone<=0.008, silent RMS<=0.002. Volume
bands are recorded in results.json. Passing is a functional routing check, not
an acoustic quality/latency or performance benchmark. Tone-analysis callbacks
are deliberately instrumented and not representative of application overhead.

Observed examples (linear peak amplitude):
- Initial headphone410Hz0.14969 and native730Hz0.14960.
- Final native mic1090Hz0.14164 in both channels;410Hz0.00015 and730Hz0.00074.
- Master0.5 headphone410Hz0.07489 and730Hz0.07430.
- Global deafen: headphone and both mic destinations RMS0.

Scenarios: initial mixed playback/fail-closed voice; voice to both applications;
Mac destination mute; Windows destination mute; Mac deafen; Windows deafen;
Mac source volume0.25; master0.5; Mac playback silence; global mute; global
deafen; undeafen restores both playback and stereo mic without leakage.

Evidence:
results.json — checks, amplitudes, binary/source hashes, redacted configs,
               limits, exits and disposable-key cleanup.
native.jsonl — actual CoreAudio BlackHole16ch stereo capture measurements.
win.jsonl — fake Windows headphone/mic measurements.
win-states.json, mac-states.json — real CLI telemetry.
helper.log, suite.log — output and exact completed checks.

Limits: both processes run natively on this Mac; the Windows role uses fake
AudioStreams. This is additional native Mac driver-path evidence, not a Windows
hardware test. No AirPods, Discord, game, network-Wi-Fi behavior, long soak,
physical device reconnect or acoustic/transport latency is tested. Only the
first two channels of the16-channel cable are exercised by this stereo system.

Path assumptions when archiving these files:
run.py derives workspace ROOT as Path(__file__).resolve().parents[2], which is
correct only at WORKSPACE/work/native-cable-integration/run.py. It then uses
ROOT/work/build-release13/{audiobridge,runtime_fake} and imports the helper
functions from ROOT/outputs/AudioBridge/tests/runtime_test.py. HERE is the
script's containing directory, and cable_helper must be compiled there.
If this evidence is archived under outputs/AudioBridge/validation/native-mac-cables,
copy it back into WORKSPACE/work/native-cable-integration before running the
commands above, or explicitly adjust ROOT and binary paths for the new layout.
The archived evidence is not presented as a relocatable installed test package.

AudioBridge routing-tone calibration (optional, explicit test utility)

The original 400/600/1000/1200 Hz set produces false isolation failures under
the existing 48 kbps microphone Opus codec with allowed clock drift. This was
reproduced without a network, queue underruns or packet-loss concealment.
Changing a threshold would obscure this measurement problem.

Calibrated replacement:
  Index 0 Windows playback: 410 Hz
  Index 1 Mac playback: 730 Hz
  Index 2 Windows microphone: 1090 Hz
  Index 3 Mac microphone: 2390 Hz

In tests/audio_fake.cpp both frequencies{} (analysis) and the hz generation
ternary use this same set. All metric indices, routing assertions, gains and thresholds stay
unchanged. Runtime test Python scripts use indices, not fixed frequencies.

The calibration uses actual Opus with unchanged production settings: 48 kHz
stereo PCM, 5 ms/240-frame packets, restricted-lowdelay, complexity 3, CBR,
128 kbps playback and 48 kbps microphone, AUTO channel selection.

Matrix: four source tones; clean plus each of the three wrong-source injections;
nine fixed drift offsets from -1000 to +1000 ppm in 250 ppm steps plus a triangular
drift that slews 10 ppm per 5 ms block; three initial waveform phases. Each case
runs 10 seconds of generated data. After about 1 second warmup, every overlapping
three-window median (100 ms per window, 89 assessments per case) is checked.

Negative controls inject a wrong-source tone with amplitude 0.015, 10% of the
wanted 0.15, BEFORE the codec. Thus codec suppression of a contaminant cannot
be hidden by injecting it only after decoding. All 12 directional contamination
pairs are exercised, including both microphone destinations' source alternatives
and microphone contamination of playback. The production routing graph itself
is still verified separately by paired runtime tests.

The 480-case reference result has zero false positives and zero missed injected
contaminants across 42,720 assessments. Largest spurious amplitude 0.002671246
is below unchanged 0.008. Smallest injected amplitude 0.012668721 is above 0.008.
Smallest wanted amplitude 0.119628689 is above unchanged 0.04. Silence threshold
0.002 is unchanged; this non-silence matrix does not separately test that gate.

Run from the AudioBridge repository root with an installed libopus prefix:
 python3 tests/calibration/run_calibration.py --opus-prefix /path/to/opus-prefix --output /path/to/fresh-output

For the existing Mac build workspace (fresh output required):
 python3 tests/calibration/run_calibration.py --opus-prefix ../../work/mac13-deps/install --output ../../work/tone-calibration/repo-reproduction-1

The runner compiles the adjacent codec_tone_calibration.cpp, executes the matrix,
checks exact coverage and thresholds, and records source/binary/result hashes.
Use explicitly for codec/tone changes. Do not add the full 480-case matrix to
every default CTest run. A host compiler and already-installed libopus are needed;
the utility does not install dependencies, access devices or open sockets.

Limits: codec-only calibration, not validation of LAN continuity, buffering,
physical microphones, AirPods, Discord, Deadlock or Bluetooth latency. Preserve
the original failed LAN runs; this corrects test attribution, not those outages.

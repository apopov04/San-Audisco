# Validation record

Development build, 17 November 2026. Host: Apple Silicon macOS 26, Apple Clang 21. Mac binaries target macOS 13. Windows cross-toolchain: official LLVM-MinGW 20261104, x86-64 UCRT. Windows execution: isolated Wine 11.0 prefix using existing Rosetta; this is not a Windows kernel or Bluetooth stack.

## Executed

- Native macOS Core Audio enumeration: built-in mono microphone and stereo speakers identified.
- Native macOS output: silence rendered for 300 ms; 13,826 callback frames observed and clean stop verified. No real microphone recording.
- Mac audio lifecycle: device invalidation, callbacks after close/reopen and a silently stalled callback clock checked with fault injection; listener lifetime and stall recovery checked under sanitizers.
- Native Mac interface: launched, device picker populated, missing-configuration error verified, scrolling layout and lower controls visually inspected.
- Routing engine: 2,048 settings combinations, microphone/playback isolation, route switches, mute preservation, volume/balance/master, clipping and invalid sample handling.
- Adaptive buffering: 90 simulated seconds per ±500/±900 ppm device-clock mismatch, callback batching, bounded overflow/underrun and concurrency.
- Transport: real localhost UDP, authenticated handshake, wrong key/tamper/replay rejection, session binding, bounded packet ordering, packet loss, Opus encode/decode, heartbeat timeout and reconnection.
- Two-engine integration: spectral distinction between microphone and playback across actual encrypted Opus packets.
- Actual application runtime: two separate CLI processes with deterministic audio-device substitutes. Seventeen scenarios cover audible output spectra, remote controls, independent mic mute/deafen, volume, balance, listening-only silence, primary switching, microphone switching, global deafen restoration, a 300 ms suspended sender, peer crash/restart and explicit unmute after reconnection.
- Control parsing: malformed, oversized and invalid values rejected atomically.
- All eleven native Mac CTest suites passed in Release and under AddressSanitizer/UndefinedBehaviorSanitizer. Concurrent release/sanitized runtime tests exposed a remote-playback attenuation bug. The fix acknowledges callback resets before advancing decoded media, keeps a separate remote queue reserve, and avoids speculative concealment during prefill. Both concurrent suites then passed twice at the original thresholds. The optional deterministic scheduler stress also passed in both builds; enable it with `BRIDGE_RUNTIME_STRESS=1` when running tests/runtime_test.py on a POSIX host. AddressSanitizer and UndefinedBehaviorSanitizer runs passed. ThreadSanitizer independently run for engine, adaptive ring and callback-driven listening mixer.
- Listening callback: arbitrary callback sizes, settings/reset concurrency, direct impulse comparison, and 108 capture/render/clock combinations (128–1536 frame quanta, -500/0/+500 ppm). No post-warmup missing output or overflow in the callback matrix.
- Optional physical routes: Windows without a local microphone and Mac without headphones pair and route correctly. Switching to missing roles produces silence without default-device fallback; returning to configured routes recovers.
- Protected credentials: bounded input, invalid identifiers, overwrite prevention, Unicode paths and key_id precedence; actual temporary Keychain and Wine DPAPI create/read/update/delete round trips. Temporary credentials removed.
- Windows PE executables under Wine: engine, control, adaptive ring, transport and integration suites passed; actual two-process Windows runtime passed 16 scenarios. POSIX SIGSTOP/SIGCONT test explicitly skipped on Windows.
- Mixed-platform pair: native arm64 macOS CLI plus Windows x64 PE CLI under Wine passed 13 playback/microphone/control/handover scenarios over real encrypted UDP and Opus, with synthetic audio endpoints.
- Windows WASAPI backend through Wine/Core Audio: 24,064 silent stereo frames rendered over 500 ms. This validates the backend code through Wine translation, not a Windows hardware driver.
- Windows backend recovery: injected a real WASAPI event-wait timeout during silent output, verified running became false, then reopened and rendered again. The normal stop event also passed.
- Native Win32 interface under Wine: 43 controls, four direction-filtered audio dropdowns populated from real enumeration, clean shutdown. GUI onboarding with synthetic audio endpoints verified Unicode key import, protected configuration, matching port 45670, and restart after the transfer key file was removed. This caught and fixed a final-stdout-drain race in device enumeration.
- Package inspection caught and fixed a macOS deployment mismatch: the plist declared 13 while initial binaries required 26. Dependency and application builds now explicitly target 13; runtime on an older Mac remains untested.

The Windows backend, CLI and GUI cross-compile and execute under Wine. The included native Windows CI workflow has not been submitted to a remote runner. No genuine Windows VM or physical Windows PC has been exercised.

## Performance evidence

A release routing-engine microbenchmark processed 200,000 five-millisecond blocks in 0.175 seconds on this Mac: roughly 0.87 microseconds per block. This excludes audio devices, resampling, encryption, codec, networking, GUI and virtual drivers. It is not an end-to-end CPU or game-performance measurement.

Codec payload: 128 kbps playback + 48 kbps microphone. Five-millisecond packets add substantial overhead; approximately 496 kbps including the authenticated envelope and IPv4/UDP for two active streams, excluding link-layer overhead and controls.

The actual paired CLI ran for 20 steady seconds (approximately 22 seconds including startup/shutdown) with synthetic 240-frame audio callbacks and expensive test tone analysis disabled. Final Windows-role process: 2.21% of one core, 6.84 MiB peak RSS. Mac-role process: 3.07%, 6.83 MiB. Both are native Mac processes; these are not Windows performance numbers. Codec, encryption, networking, routing and callbacks are included. Native drivers, GUI, Bluetooth and the game are excluded. The repeatable harness is `tests/runtime_benchmark.py`; `validation/runtime-benchmark-final.json` records the final run. Startup can count rejected pre-handshake packets during asymmetric readiness; this is not a measure of steady-state packet loss.

A deterministic impulse comparison at identical 1536-frame targets measured 59.812 ms through the prior two-queue path versus 29.917 ms through the callback-driven listening path, removing 29.896 ms. Actual default local listening targets now use the larger reported callback quantum plus 240 frames; remote playback has a separate minimum 960-frame queue; microphone and network-transmit queues retain 1536 frames. The Mac output API reported a 514-frame converted callback hint. No physical loopback latency has been measured, and the earlier proposed ≤20 ms added local game latency is not yet established.

## Not yet established

Genuine Windows runtime/driver behavior; VB-CABLE installation/latency/compatibility; actual Discord and Deadlock integration; AirPods microphone quality and Bluetooth transitions; end-to-end latency; Windows CPU/RAM and frame-time impact; two-hour hardware soak; production signing/notarization.

The build is an experimental alpha. Passing software simulation establishes routing and protocol behavior, not gaming readiness.

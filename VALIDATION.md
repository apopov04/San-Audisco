# Validation record

Development build, 17 November 2026. Host: Apple Silicon macOS, Apple Clang 21. Windows cross-toolchain: official LLVM-MinGW 20261104, x86-64 UCRT.

## Executed

- Native macOS Core Audio enumeration: built-in mono microphone and stereo speakers identified.
- Native macOS output: silence rendered for 300 ms; 13,826 callback frames observed and clean stop verified. No real microphone recording.
- Mac audio lifecycle: device invalidation and callbacks after close/reopen checked with fault injection; listener lifetime checked under sanitizers.
- Native Mac interface: launched, device picker populated, missing-configuration error verified, scrolling layout and lower controls visually inspected.
- Routing engine: 2,048 settings combinations, microphone/playback isolation, route switches, mute preservation, volume/balance/master, clipping and invalid sample handling.
- Adaptive buffering: 90 simulated seconds per ±500/±900 ppm device-clock mismatch, callback batching, bounded overflow/underrun and concurrency.
- Transport: real localhost UDP, authenticated handshake, wrong key/tamper/replay rejection, session binding, bounded packet ordering, packet loss, Opus encode/decode, heartbeat timeout and reconnection.
- Two-engine integration: spectral distinction between microphone and playback across actual encrypted Opus packets.
- Actual application runtime: two separate CLI processes with deterministic audio-device substitutes. Seventeen scenarios cover audible output spectra, remote controls, independent mic mute/deafen, volume, balance, listening-only silence, primary switching, microphone switching, global deafen restoration, a 300 ms suspended sender, peer crash/restart and explicit unmute after reconnection.
- Control parsing: malformed, oversized and invalid values rejected atomically.
- AddressSanitizer and UndefinedBehaviorSanitizer runs; ThreadSanitizer independently run for engine and adaptive ring.

The native Windows backend, CLI, GUI and initial C++ test binaries cross-compile. Windows executables have not been run on Windows. The included CI workflow has not been submitted to a remote runner.

## Performance evidence

A release routing-engine microbenchmark processed 200,000 five-millisecond blocks in 0.175 seconds on this Mac: roughly 0.87 microseconds per block. This excludes audio devices, resampling, encryption, codec, networking, GUI and virtual drivers. It is not an end-to-end CPU or game-performance measurement.

Codec payload: 128 kbps playback + 48 kbps microphone. Five-millisecond packets add substantial overhead; approximately 496 kbps including the authenticated envelope and IPv4/UDP for two active streams, excluding link-layer overhead and controls.

Default hardware queue targets are 1536 frames (32 ms each). The resulting path can add roughly 64 ms across capture and playback, plus driver/network buffering. This does not meet the earlier proposed ≤20 ms added local latency. The configurable buffer target is a tuning mechanism, not proof that smaller buffers work on a particular device.

## Not yet established

Windows native runtime behavior; VB-CABLE installation/latency/compatibility; actual Discord and Deadlock integration; AirPods microphone quality and Bluetooth transitions; end-to-end latency; Windows CPU/RAM and frame-time impact; two-hour hardware soak; production signing/notarization.

The build is an experimental alpha. Passing software simulation establishes routing and protocol behavior, not gaming readiness.

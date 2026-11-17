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

The Windows backend, CLI and GUI cross-compile and execute under Wine. The included native Windows CI workflow has not been submitted to a remote runner.

### Physical Windows PC, 17 November 2026

The pre-buffer-update release (fa8ed95) and a separately packaged test kit were transferred with SHA256 verification to the user's Windows 10 Home build 19045 PC, Intel i7-7700 (4 cores / 8 logical processors). Native CLI device enumeration passed. Connected AirPods exposed stereo 44.1 kHz playback and separate mono 8 kHz hands-free endpoints; the microphone remained closed.

All 15 native test groups passed: seven portable C++ suites, actual DPAPI storage, CLI credentials, 16 synthetic paired-runtime scenarios, optional routes, WASAPI output/recovery and two benchmarks. The POSIX scheduler-suspension scenario was explicitly skipped. Tests ran sequentially. The WASAPI checks explicitly selected the physical AirPods stereo endpoint, rendered 24,514 silent frames using the production 48 kHz backend, and verified reopening after an injected event-wait timeout. This is silent output and simulated timeout evidence, not an audible Bluetooth disconnect/reconnect or latency measurement. Full results and test-binary hashes: `validation/windows-native-2026-11-17.log`.

The two synthetic runtime roles both ran on that Windows PC for a 10.043-second steady interval. Windows role: 1.556% of one logical core, 7.160 MiB working set; Mac role: 3.423%, 7.180 MiB. CPU uses GetProcessTimes user+kernel deltas after pairing/settling; memory is working set, not private bytes. Opus, encrypted localhost UDP, mixing and synthetic callbacks are included; native audio, Bluetooth, GUI, Discord and Deadlock are excluded. These numbers do not establish game performance or the final hardware configuration's resource use.

### First two-machine Wi-Fi run: failed acceptance

The native Mac and Windows test runtimes paired across the user's LAN using synthetic audio endpoints. Mac passed 15 of 16 phases; remote audio failed to resume during its global-undeafen check. Windows initially reported 16 passes, but that harness stopped checking each phase after its first success. Its full telemetry revealed a subsequent connection loss in the same phase, followed by reconnect with the microphone correctly remaining muted. Overall acceptance therefore failed. The initial cause of the connection loss is unresolved; neither a network fault nor a runtime fault has yet been isolated. Preserve this failure when evaluating follow-up diagnostic runs. Evidence: `validation/lan-first-mac/` and `validation/lan-first-windows-transition.log`.

Native Mac BlackHole 2ch has been installed and a two-tone stereo loopback passed through the production Core Audio backend, without opening physical microphones or speakers. Evidence: `validation/blackhole-2ch-loopback.log`. A Windows Steam Streaming Microphone endpoint pair did not return the probe tones and is not a validated virtual cable substitute.

Windows WLAN events subsequently showed security stopping at 18:58:17.130 UTC and succeeding at 18:58:18.968, overlapping AudioBridge's 18:58:19.494 disconnected report. A later driver-reported WLAN disconnect occurred at 18:58:47.610 after the run ended. This supports Wi-Fi instability as a likely contributor, without identifying the driver/root cause or excluding a software issue. Sanitized evidence: `validation/windows-wifi-transition.log`. The user selected continued testing over Wi-Fi.

A follow-up used copied-source transport tracing and a stricter continuous-assessment harness with acknowledgement from both hosts. Production binaries were unchanged. It also failed: Mac 15/16, Windows 7/16. This time there were no transport disconnects or send/receive/decode errors. Brief remote-audio attenuation and unexpected spectral energy exceeded the existing thresholds. Windows' largest recorded worker-poll gap was 36.187 ms. The remaining possibilities include scheduling, Wi-Fi packet bursts, buffering and test-backend behavior; attribution is pending. Evidence: `validation/lan-diagnostic-mac/` and `validation/lan-diagnostic-windows/`. Do not treat the original Windows 16/16 early-pass result as sustained LAN reliability.

Further isolated measurements separated three issues:

- Native Windows 5 ms deadline pacing with standard sleep produced 16.087 ms p99 intervals and 1,353 sub-0.25 ms catch-up intervals in 2,000 samples. A high-resolution waitable timer produced 5.888 ms p99 and no such bursts. Ten-second CPU counters quantized to zero for both loops; this does not mean zero CPU cost. Evidence: `validation/windows-timer-comparison.json`.
- A traced high-resolution runtime candidate still failed LAN acceptance (Windows 7/16, Mac 14/16). Its ordinary Windows worker gaps improved to approximately 6 ms, but received playback/microphone gaps reached 75.667/79.704 ms, followed by 14/15-packet bursts. These exceed the 20/15 ms receive reservoirs and 12-packet reorder capacity. Real receive-ring underruns were recorded. An isolated 33 ms scheduling stall also remained. Evidence: `validation/windows-timing-analysis.json`, `validation/lan-timing-mac/` and `validation/lan-timing-windows/`.
- Separate codec-only calibration showed that the original adjacent test frequencies could produce false wrong-route detections through lossy Opus plus permitted sample-rate drift, without any cross-routing. A new 410/730/1090/2390 Hz set passed 480 calibration cases and 42,720 three-window assessments with unchanged thresholds: zero false positives and zero missed deliberately injected 10% contamination. Maximum unwanted amplitude was 0.002671 (limit 0.008); minimum injected amplitude 0.012669 (above that limit). Evidence: `validation/tone-calibration-summary.json`. This measurement correction does not erase the independently observed underruns.

### Buffered Wi-Fi candidate: both hosts passed

The next candidate retained the high-resolution Windows timer, increased only remote playback/microphone reserves to 100 ms and reorder capacity to 32 packets, and used calibrated test tones. Both hosts passed all 16 continuously assessed routing/control phases, including global undeafen, in an approximately 82-second synthetic LAN run. Windows reported no transport timeouts, socket/decode errors, or remote-listening, network-microphone or microphone-output underruns/drops. The local listening queue recorded 240 underrun frames and dropped frames while its output role was inactive; this is not an all-queue zero-loss claim. Evidence: `validation/lan-buffer-mac/` and `validation/lan-buffer-windows/`.

This passing run had maximum Windows receive gaps of 28.564 ms (playback) and 25.021 ms (microphone), milder than the prior approximately 80 ms gaps. It cannot alone isolate the buffer change's contribution. A deterministic production regression therefore delivers eight reversed 16-packet bursts at 80 ms intervals, drains them using the runtime's 12-packet-per-tick bound, and checks continuous playback/microphone samples and exact media order. It passed without resets or microphone underruns/drops, while a local impulse crossed the local path in 5 ms of simulated time. This is queue behavior, not measured physical latency.

The minimal changes have been promoted to the normal build. Playback/microphone codecs remain at 128/48 kbps; test thresholds are unchanged. The remote queue targets add approximately 80/85 ms compared with the previous targets. Total audible network and Bluetooth latency remains unmeasured. Native Mac release rebuild and all 11 CTests passed after promotion. AddressSanitizer/UndefinedBehaviorSanitizer builds also passed the affected runtime, optional-route and listening-mixer suites. The optional reproducible codec calibration is in tests/calibration; its documented invocation reproduced all 480 cases. Fresh native Windows verification is recorded below.

### Production rebuild on native Windows

The freshly rebuilt production-source kit passed all 15 groups, exit 0, including the deterministic burst regression, routing, DPAPI and explicit AirPods stereo silent rendering/recovery. The POSIX-only suspension scenario remained skipped. The silent render delivered 24,034 frames. Binary/source hashes and full output are in `validation/windows-native-v2/`.

Deadlock was already running and was left untouched throughout. The 10.051-second synthetic paired benchmark reported approximately 7.680/7.711 MiB working sets for the Windows/Mac roles, respectively. GetProcessTimes returned zero user/kernel deltas for both; these readings do not establish zero CPU cost or a useful CPU estimate. Both synthetic roles ran on the PC. No native-routing CPU, frame-time or FPS result follows from this run.

## Performance evidence

A release routing-engine microbenchmark processed 200,000 five-millisecond blocks in 0.175 seconds on this Mac: roughly 0.87 microseconds per block. This excludes audio devices, resampling, encryption, codec, networking, GUI and virtual drivers. It is not an end-to-end CPU or game-performance measurement.

Codec payload: 128 kbps playback + 48 kbps microphone. Five-millisecond packets add substantial overhead; approximately 496 kbps including the authenticated envelope and IPv4/UDP for two active streams, excluding link-layer overhead and controls.

The actual paired CLI ran for 20 steady seconds (approximately 22 seconds including startup/shutdown) with synthetic 240-frame audio callbacks and expensive test tone analysis disabled. Final Windows-role process: 2.21% of one core, 6.84 MiB peak RSS. Mac-role process: 3.07%, 6.83 MiB. Both are native Mac processes; these are not Windows performance numbers. Codec, encryption, networking, routing and callbacks are included. Native drivers, GUI, Bluetooth and the game are excluded. The repeatable harness is `tests/runtime_benchmark.py`; `validation/runtime-benchmark-final.json` records the final run. Startup can count rejected pre-handshake packets during asymmetric readiness; this is not a measure of steady-state packet loss.

A deterministic impulse comparison at identical 1536-frame targets measured 59.812 ms through the prior two-queue path versus 29.917 ms through the callback-driven listening path, removing 29.896 ms. Actual default local listening targets now use the larger reported callback quantum plus 240 frames; remote playback now has a separate minimum 4800-frame queue; microphone and network-transmit queues retain 1536 frames. The Mac output API reported a 514-frame converted callback hint. No physical loopback latency has been measured, and the earlier proposed ≤20 ms added local game latency is not yet established.

## Not yet established

Complete two-machine physical audio routing; virtual-cable installation/latency/compatibility; actual Discord and Deadlock integration; AirPods microphone quality and Bluetooth transitions; end-to-end latency; resource use with native audio and game frame-time impact; two-hour hardware soak; production signing/notarization.

The build is an experimental alpha. Passing software simulation establishes routing and protocol behavior, not gaming readiness.

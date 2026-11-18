# v0.2 validation — 2026-11-18

This record separates software checks from the remaining hardware acceptance.
The working v0.1 baseline is commit `32ae9ccb81a0692b9d2846316938a74980bd43fa`.
The user reported successful Discord-on-Mac / Deadlock-on-Windows audio with that
version after driver installation. That report does not establish v0.2 installer,
latency, performance, or reboot correctness.

## Completed software validation

The final native macOS CTest run passed **19/19**, including:

- Engine route gates, mixing, clock compensation, listening mixer, and controls.
- Encrypted audio transport, process integration, optional hardware routes and
  existing callback lifecycle fault handling.
- Credentials and legacy key-file CLI behavior.
- Room discovery/manual probe, eight-character invitation parsing, correct/wrong
  PAKE codes, expiry, cancellation, attempt limits, encrypted control, reconnect,
  third-member rejection, payload bounds, ciphertext tampering/replay and changed
  handshake identity/port rejection.
- Two separate agents with production engines and synthetic audio: singleton,
  pairing, inventory, remote rename/setup, invalid-direction rejection, shared
  start/stop, mixer acknowledgement, meters, explicit remote test sound, persisted
  reconnect, protective mute, repeat Quit, and unauthenticated IPC rejection.
- Child process blocked-input pressure, bounded shutdown and repeated restart.
- Automatic route selection, ambiguous candidates, saved choices, wrong direction,
  same-cable feedback rejection, and required Hi-Fi Cable sample rate.
- Setup decision logic plus package pinning and unsafe archive rejection.
- Private key-pipe framing: short, malformed, non-hexadecimal, NUL, oversized and
  delimiter errors fail before engine startup. Valid keys and commands coalesced
  in the same pipe write are preserved. A 440 Hz test tone reaches headphones
  without entering either active, unmuted microphone feed.

Windows x64 cross-compilation succeeded for all three production binaries.
Windows PE room, wire-tamper, child-process, agent lifecycle, and key-pipe/tone
checks also passed under Wine using its isolated prefix and Windows embedded
Python. Windows credential tests exercise the DPAPI API implementation available
under Wine; this is not native Windows security/driver validation.

The actual Win32 UI was driven under Wine against a test-only agent fixture,
including pairing states, device choices, controls, pending-stop cancellation,
reconnect action, meters, explicit test tone, close-to-background and Quit. The
production macOS model passed its JSON/IPC fixture tests, and the production Swift
UI compiled. Windows UI compilation treats warnings as errors. Room pairing and
wire tests also passed AddressSanitizer/UndefinedBehaviorSanitizer on macOS.

No test fixture or `runtime_fake` executable belongs in the app or installer.
Automated signal tests synthesize known frequencies; they do not record physical
microphone content. Private package construction and verification do not install
or launch the payloads.

## Lightweight operation check

A separate 15.05-second native macOS check ran the real unpaired agent with actual
endpoint enumeration and no audio streams/UI. It used 0.04 seconds of process CPU
(approximately 0.27% of one core over that short interval) and 10,448 KiB resident
memory at the sample. This is a small idle-agent measurement, not a Windows/game,
paired-room, GUI, thermal, battery, or long-duration benchmark.

The engine's codec, 5 ms frame size, Wi-Fi reserves and local listening queue
settings were retained. This release does not claim lower audible latency. See
[ENGINE.md](ENGINE.md) for the existing buffers and historical synthetic timings.

## Remaining acceptance on the real computers

The Windows remote host was unavailable during this build. The working apps were
not replaced, drivers were not reinstalled, and neither computer was restarted.
The following therefore remain **unverified for v0.2**:

1. Private installer behavior on clean and already-configured Windows 10/macOS,
   including actual UAC, vendor UI, signature trust on the target PC, cancellation,
   restart/resume, rollback, and OS quarantine/permission prompts.
2. End-to-end physical audio with the AirPods on the Windows Hub, MacBook microphone,
   Discord on Mac and Deadlock on Windows; both mic feeds and feedback isolation.
3. Hub/mic-source changes, physical unplug/replug, sleep/wake, AirPods reconnection,
   Wi-Fi interruptions, and two-hour endurance on actual drivers.
4. Audible Bluetooth/network latency and Deadlock frame-time impact on the i7-7700.
5. Actual login-item approval/startup and macOS execution at the older minimum OS.

Use the room README and [HARDWARE_TEST.md](../HARDWARE_TEST.md) to conduct these
checks. Keep v0.1 available until they pass. Wine is a compatibility layer, not a
Windows VM with the target Bluetooth and audio drivers. Compilation, synthetic
signals, and archive integrity cannot establish the results above.

The GitHub workflow is a reproducible native Windows CI recipe; its results are
separate from this local test record. No paid signing/notarization certificates
or redistribution permissions were purchased or assumed.

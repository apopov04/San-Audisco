# AudioBridge

Experimental native Windows and macOS audio bridge. It mixes two computers into one headphone output and feeds one microphone to voice applications on both computers. The primary computer owns headphone playback; the microphone source is selected independently.

This is a development build. It has automated audio-routing, real encrypted localhost network, codec, clock-drift and native macOS silent-playback tests. It has not been validated with a Windows audio device, AirPods, Discord or Deadlock. Windows binaries are cross-compiled; this is not Windows runtime validation. Hardware testing remains necessary before calling it suitable for gaming.

## Files and launch

- Mac: `dist/AudioBridge.app`. Open it, select the four explicit audio devices, enter the Windows computer's numeric IPv4 address, select a shared pairing-key file, and start.
- Windows: `dist/windows/AudioBridgeApp.exe` launches the adjacent `audiobridge.exe`. Open Setup, choose the peer address, shared key and four device routes, then start. An advanced configuration file remains optional.
- CLI: `audiobridge devices`, `audiobridge keygen FILE`, `audiobridge run CONFIG`.

The apps are development signed on Mac and unsigned on Windows; no trusted distribution certificate or notarization has been supplied. Do not disable system security to run them. Build from source or use your normal developer approval flow.

## Required audio devices

Install two independent virtual cables on each computer. The current setup targets the official VB-CABLE A+B packages, separately licensed from https://vb-audio.com/Cable/ . No driver installers are bundled. BlackHole or other equivalent independent virtual paths can also be selected, but have not been tested here.

The two cables must be separate:

| Path | Application configuration | AudioBridge configuration |
|---|---|---|
| Cable A playback | Deadlock/Discord output = Cable A playback endpoint | playback_input = Cable A recording endpoint |
| Cable B microphone | Deadlock/Discord input = Cable B recording endpoint | microphone_output = Cable B playback endpoint |
| Headphones | Connect AirPods to the selected primary computer | headphones_output = physical AirPods playback endpoint |
| Physical microphone | Select one actual microphone on the designated source computer | physical_microphone = its input endpoint |

Never choose Cable B as the playback source, Cable A as the microphone destination, or a virtual microphone as the physical microphone source. Do not enable Windows “Listen to this device.” Keep the AirPods physical microphone closed if using the MacBook microphone for better playback quality.

The initial primary is Windows; the initial microphone source is Mac. The session begins globally muted. Once both computers connect, explicitly unmute. Primary/source changes and reconnection mute the session again. Connect AirPods through operating-system Bluetooth settings before switching primary. This build does not implement automatic Bluetooth switching.

## Pairing

Generate one 32-byte random key using the Mac button or `audiobridge keygen pairing.key`. Transfer that file privately to the other computer. Select the local path on each device. This is a manual pairing-token prototype; QR discovery, Keychain/DPAPI storage, and friendly pairing verification remain future work. The secret is never written to logs or command arguments by the application. The key file must be protected on both computers.

Both apps use UDP 45670 by default, and each is configured with the other's numeric IPv4 address. Permit the executable through the private-network firewall. Guest network client isolation can prevent connectivity. There are no cloud relays or accounts.

## Configuration

Run `audiobridge devices` on each computer to obtain stable device IDs. Create a UTF-8 file with these exact fields (no quotes around values):

```ini
device=windows
peer=192.168.1.20
port=45670
peer_port=45670
key_file=C:\Users\you\Documents\pairing.key
playback_input=ID_OF_CABLE_A_RECORDING_ENDPOINT
microphone_output=ID_OF_CABLE_B_PLAYBACK_ENDPOINT
headphones_output=ID_OF_AIRPODS_PLAYBACK_ENDPOINT
physical_microphone=ID_OF_PHYSICAL_MICROPHONE
```

On Mac use `device=mac`, the Windows peer address and Mac device IDs/paths. The native Mac interface writes this configuration to Application Support/AudioBridge. At present all four fields must be configured even when that machine is secondary.

CLI accepts newline commands while running:

```text
primary 0
mic_source 1
win_volume 0.8
mac_volume 0.6
balance -0.2
win_mute 1
mac_mute 0
win_deafen 0
mac_deafen 1
win_silence 1
master 0.9
mute 0
deafen 0
quit
```

Device values: 0 Windows, 1 Mac. `mute` blocks microphone to both. Route mute only blocks the microphone destined for that route. `silence` only blocks listening; `deafen` blocks listening and microphone without changing the stored mute flag. Balance -1 favors Windows, +1 favors Mac, center preserves both levels. These controls do not change Discord's displayed mute/deafen icons or override the game's own push-to-talk.

## Build

Requirements: CMake 3.20+, C++20 compiler, libsodium 1.0.20, Opus 1.5.2. On Mac also Swift/Xcode command-line tools.

```sh
./scripts/bootstrap-deps.sh /absolute/path/to/deps
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSODIUM_ROOT=/absolute/path/to/deps
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./scripts/package-mac.sh "$PWD/build"
```

The dependency bootstrap downloads official archives and verifies pinned SHA256 values before building. Windows requires MSVC or llvm-mingw and equivalent dependency libraries. `.github/workflows/windows.yml`, when present, is the native Windows CI recipe; adding a workflow does not mean it has run.

For sanitizers configure a separate build with `-DBRIDGE_SANITIZE=ON`. The output includes test binaries; no tests record real microphone content.

## Architecture and implementation status

C++ engine: stereo float 48kHz buses, microphone mono fold-down, independent route gates, gain ramps, peak limiter, atomic settings snapshots. The audio callbacks use preallocated queues. The worker handles networking, codec, routing and state replication. A bounded adaptive resampler corrects device clock differences. The current linear interpolation resampler is functional but needs a filtered replacement for optimal quality.

Native audio: macOS Core Audio HAL/AudioUnit; Windows WASAPI shared-mode event callbacks. GUI: SwiftUI on Mac and Win32 on Windows. Physical devices are explicitly selected; the app does not change system defaults.

Transport: libsodium XChaCha20-Poly1305 with a pre-shared random key, fresh session epochs, recipient binding and anti-replay window. Opus stereo at 128kbps for playback and 48kbps for microphone, 5ms frames, complexity 3. Bounded jitter buffers reorder packets. Controls are periodically replicated with version/acknowledgement, with Windows as coordinator regardless of audio primary. Local-only LAN, numeric IPv4.

The initial hardware buffer targets are conservative: 1536 frames (32 ms) per hardware boundary. Capture and playback together can add roughly 64 ms before virtual-driver and network buffering. Optional `buffer_frames=256..2048` in the configuration permits measured tuning; values too small for actual callback sizes can cause dropouts. Total game latency and CPU load are not measured on the target PC, and may fail the original low-latency targets. The core has real-time discipline, but this implementation is not a guarantee of lower overhead than Discord on your hardware.

Outstanding product work includes device discovery and secure credential storage, acknowledged controls reflected consistently in both interfaces, complete device-loss recovery, production signing, filtered resampling, end-to-end latency measurements and performance tuning. The iPhone full audio-routing role is unsupported.

## Hardware acceptance checklist

1. Run Deadlock alone and record frame-time percentiles, CPU and RAM. Repeat with Discord on Windows, and then with AudioBridge + Discord on Mac.
2. Validate Cable A and B independently on both machines; use application mic tests and test tones. Confirm no playback leaks into either microphone feed.
3. Test all mute/deafen routes and the global controls. Verify reconnection/source changes remain muted until explicitly enabled.
4. Test Windows-primary and Mac-primary with AirPods mic and MacBook mic separately. Note actual formats and perceived quality changes.
5. Measure added local game delay against direct AirPods playback, and network audio delay with timestamped/correlated recordings. Hardware Bluetooth latency is additional.
6. Run for two hours; test Wi-Fi interruptions, peer restart, sleep/wake, AirPods removal and Bluetooth reconnection.
7. Reject gaming readiness if frame-time impact or audio latency is unacceptable. Automated localhost tests cannot establish these results.

No physical microphone recording or driver installation has been performed during automated tests.

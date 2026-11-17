# AudioBridge

Experimental native Windows and macOS audio bridge. It mixes two computers into one headphone output and feeds one microphone to voice applications on both computers. The primary computer owns headphone playback; the microphone source is selected independently.

This is a development build. Native Mac tests, native Windows 10 tests on the target i7-7700 PC, Windows executables under Wine, and a mixed Mac/Wine process pair pass routing and encrypted-network tests. Native Windows WASAPI has rendered silence to the connected AirPods stereo endpoint and recovered from an injected timeout. The synthetic Mac/Windows pair also passed all 16 continuous routing phases over Wi-Fi after the receive-buffer fix. Complete two-machine hardware routing, audible AirPods latency, Discord and Deadlock remain unverified. Hardware testing is required before calling it suitable for gaming; see VALIDATION.md for the exact evidence.

## Files and launch

- Mac: `dist/AudioBridge.app`. Open it, select the virtual cables and any physical audio devices this Mac will use, enter the Windows computer's numeric IPv4 address, select a shared pairing-key file, and start.
- Windows: `dist/windows/AudioBridgeApp.exe` launches the adjacent `audiobridge.exe`. Open Setup, choose the peer address, shared key and audio routes, then start. An advanced configuration file remains optional.
- CLI: `audiobridge devices`, `audiobridge keygen FILE`, `audiobridge key-import ID FILE`, `audiobridge key-delete ID`, `audiobridge run CONFIG`.
- Mac builds target Apple Silicon and macOS 13 or newer; execution has only been tested on this macOS 26 host. Windows builds target x64 and require Windows 10 version 1803 or newer (including Windows 11). Native execution has been tested on Windows 10 build 19045.

The apps are development signed on Mac and unsigned on Windows; no trusted distribution certificate or notarization has been supplied. Do not disable system security to run them. Build from source or use your normal developer approval flow.

## Required audio devices

Install two independent virtual cables on each computer. On Mac, use BlackHole 2ch and BlackHole 16ch as separate paths; the 2ch path has passed a native loopback test. On Windows, the official VB-CABLE and Hi-Fi Cable provide separate paths (https://vb-audio.com/Cable/); their installation and loopback validation are still pending. Hi-Fi Cable requires matching input/output sample rates; use 48 kHz. Separately licensed VB-CABLE A+B is another option. No driver installers are bundled.

The two cables must be separate:

| Path | Application configuration | AudioBridge configuration |
|---|---|---|
| Cable A playback | Deadlock/Discord output = Cable A playback endpoint | playback_input = Cable A recording endpoint |
| Cable B microphone | Deadlock/Discord input = Cable B recording endpoint | microphone_output = Cable B playback endpoint |
| Headphones | Connect AirPods to the selected primary computer | headphones_output = physical AirPods playback endpoint |
| Physical microphone | Select one actual microphone on the designated source computer | physical_microphone = its input endpoint |

Never choose Cable B as the playback source, Cable A as the microphone destination, or a virtual microphone as the physical microphone source. Do not enable Windows “Listen to this device.” Keep the AirPods physical microphone closed if using the MacBook microphone for better playback quality.

For the Windows 10 / i7-7700 / AirPods Pro 3 target, follow HARDWARE_TEST.md.

The initial primary is Windows; the initial microphone source is Mac. The session begins globally muted. Once both computers connect, explicitly unmute. Primary/source changes and reconnection mute the session again. Connect AirPods through operating-system Bluetooth settings before switching primary. This build does not implement automatic Bluetooth switching.

## Pairing

Generate one 32-byte random key using either app or `audiobridge keygen pairing.key`. Transfer that file privately to the other computer and select its local path on each device. On Start, each interface imports the key into protected storage: Keychain on Mac, current-user DPAPI on Windows. The saved configuration contains only `key_id=paired-peer`. Once imported on both devices, the transfer copies can be deleted; they are never deleted automatically. The secret is never written to logs or command arguments. Protect the transfer file while it exists.

CLI users can run `audiobridge key-import paired-peer pairing.key` and use `key_id=paired-peer` in the configuration. Legacy `key_file=PATH` remains supported; `key_id` takes precedence. `key-delete paired-peer` forgets the local credential. Automatic discovery and QR pairing are not implemented.

Both apps use UDP 45670 by default, and each is configured with the other's numeric IPv4 address. Permit the executable through the private-network firewall. Guest network client isolation can prevent connectivity. There are no cloud relays or accounts.

## Configuration

Run `audiobridge devices` on each computer to obtain stable device IDs. Create a UTF-8 file with these exact fields (no quotes around values):

```ini
device=windows
peer=192.168.1.20
port=45670
peer_port=45670
key_id=paired-peer
playback_input=ID_OF_CABLE_A_RECORDING_ENDPOINT
microphone_output=ID_OF_CABLE_B_PLAYBACK_ENDPOINT
headphones_output=ID_OF_AIRPODS_PLAYBACK_ENDPOINT
physical_microphone=ID_OF_PHYSICAL_MICROPHONE
```

On Mac use `device=mac`, the Windows peer address and Mac device IDs/paths. The native Mac interface writes this configuration to Application Support/AudioBridge. The two virtual cable fields are required. Headphones and a physical microphone can be omitted on a computer that does not own those roles. An unconfigured headphone role produces no listening output; an unconfigured microphone source captures no voice; stop the local app, configure the device, and restart to use it. Mixer controls become available once the engine starts (Mac: once connected).

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

C++ engine: stereo float 48kHz buses, microphone mono fold-down, independent route gates, gain ramps, peak limiter, atomic settings snapshots. The audio callbacks use preallocated queues. Headphone playback mixes directly in its output callback, so the local listening path crosses one queue. A separate capture queue feeds the worker, which handles networking, codec, microphone routing and state replication. A bounded adaptive resampler corrects device clock differences. The current linear interpolation resampler is functional but needs a filtered replacement for optimal quality.

Native audio: macOS Core Audio HAL/AudioUnit; Windows WASAPI shared-mode event callbacks. GUI: SwiftUI on Mac and Win32 on Windows. Physical devices are explicitly selected; the app does not change system defaults.

Transport: libsodium XChaCha20-Poly1305 with a pre-shared random key, fresh session epochs, recipient binding and anti-replay window. Opus stereo at 128kbps for playback and 48kbps for microphone, 5ms frames, complexity 3. Bounded jitter buffers reorder packets. Controls are periodically replicated with version/acknowledgement, with Windows as coordinator regardless of audio primary. Local-only LAN, numeric IPv4. The Windows worker uses its own high-resolution waitable timer for 5 ms deadlines; it does not change the system timer resolution.

Listening buffers automatically follow the larger capture/render callback hint plus 240 frames (5 ms), bounded to 256–4096 frames. If the device cannot report its callback size, the target is 1536 frames (32 ms). Remote playback has its own queue target of at least 4800 frames (100 ms), so packet jitter does not increase the local game buffer. Received microphone audio also has a 4800-frame reserve. The 32-packet reorder queues and remote reserves cover the observed 80 ms Wi-Fi bursts; they add remote delay without enlarging the local game queue. Network transmission and microphone hardware queues retain conservative 1536-frame targets. Optional `buffer_frames=256..2048` overrides the hardware/local-listening targets for measured tuning; the remote playback minimum remains 4800 frames. Targets below the actual callback size can cause dropouts. A same-buffer simulated impulse test reduced local queue delay from 59.812 ms to 29.917 ms by removing one queue; this is not a measured AirPods latency.

Before the Wi-Fi buffering update, the paired CLI, using synthetic audio callbacks on this Mac, used approximately 2.2% (Windows role) and 3.1% (Mac role) of one CPU core, with about 6.8 MiB peak resident memory per process during a 20-second steady run. This includes codec, encrypted networking and routing, but excludes native drivers, GUI, Bluetooth and games. It does not establish performance on the target i7-7700 Windows PC.

Both interfaces reflect acknowledged controls. Failed audio streams retry; missing callbacks for two seconds trigger recovery, and macOS format/device changes invalidate streams for reopening. Recovery has automated fault-injection coverage but still requires physical unplug/sleep/Bluetooth testing. Remaining work includes automatic discovery, friendly pairing verification, filtered resampling, production signing, hardware latency measurements and performance tuning. The iPhone full audio-routing role is unsupported.

## Hardware acceptance checklist

1. Run Deadlock alone and record frame-time percentiles, CPU and RAM. Repeat with Discord on Windows, and then with AudioBridge + Discord on Mac.
2. Validate Cable A and B independently on both machines; use application mic tests and test tones. Confirm no playback leaks into either microphone feed.
3. Test all mute/deafen routes and the global controls. Verify reconnection/source changes remain muted until explicitly enabled.
4. Test Windows-primary and Mac-primary with AirPods mic and MacBook mic separately. Note actual formats and perceived quality changes.
5. Measure added local game delay against direct AirPods playback, and network audio delay with timestamped/correlated recordings. Hardware Bluetooth latency is additional.
6. Run for two hours; test Wi-Fi interruptions, peer restart, sleep/wake, AirPods removal and Bluetooth reconnection.
7. Reject gaming readiness if frame-time impact or audio latency is unacceptable. Automated localhost tests cannot establish these results.

No physical microphone recording has been performed during automated tests. BlackHole 2ch was installed separately through its normal installer. Wine tests use an isolated prefix; Wine is a compatibility layer, not a Windows VM.

# Engine reference

The v0.2 native interfaces use the room agent. This reference documents the
advanced standalone engine and the retained audio path. It is not the normal
room setup procedure. The new agent passes its derived media key through a
private inherited stdin pipe using `key_stdin=1`; legacy `key_id` and `key_file`
configuration remain supported for standalone use.

## Configuration

Run `san-audisco devices` on each computer to obtain stable device IDs. Create a UTF-8 file with these exact fields (no quotes around values):

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

On Mac use `device=mac`, the Windows peer address and Mac device IDs/paths. Legacy interfaces wrote this configuration to Application Support/AudioBridge; v0.2 writes its private engine configuration under room-v2. The two virtual cable fields are required. Headphones and a physical microphone can be omitted on a computer that does not own those roles. An unconfigured headphone role produces no listening output; an unconfigured microphone source captures no voice; stop the local app, configure the device, and restart to use it. Mixer controls become available once the engine starts (Mac: once connected).

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


## Architecture and implementation status

C++ engine: stereo float 48kHz buses, microphone mono fold-down, independent route gates, gain ramps, peak limiter, atomic settings snapshots. The audio callbacks use preallocated queues. Headphone playback mixes directly in its output callback, so the local listening path crosses one queue. A separate capture queue feeds the worker, which handles networking, codec, microphone routing and state replication. A bounded adaptive resampler corrects device clock differences. The current linear interpolation resampler is functional but needs a filtered replacement for optimal quality.

Native audio: macOS Core Audio HAL/AudioUnit; Windows WASAPI shared-mode event callbacks. GUI: SwiftUI on Mac and Win32 on Windows, communicating with the persistent room agent. Physical devices are explicitly selected; the app does not change system defaults.

Transport: libsodium XChaCha20-Poly1305 with a pre-shared random key, fresh session epochs, recipient binding and anti-replay window. Opus stereo at 128kbps for playback and 48kbps for microphone, 5ms frames, complexity 3. Bounded jitter buffers reorder packets. Controls are periodically replicated with version/acknowledgement, with Windows as coordinator regardless of audio primary. Local-only LAN, numeric IPv4. The Windows worker uses its own high-resolution waitable timer for 5 ms deadlines; it does not change the system timer resolution.

Listening buffers automatically follow the larger capture/render callback hint plus 240 frames (5 ms), bounded to 256–4096 frames. If the device cannot report its callback size, the target is 1536 frames (32 ms). Remote playback has its own queue target of at least 4800 frames (100 ms), so packet jitter does not increase the local game buffer. Received microphone audio also has a 4800-frame reserve. The 32-packet reorder queues and remote reserves cover the observed 80 ms Wi-Fi bursts; they add remote delay without enlarging the local game queue. Network transmission and microphone hardware queues retain conservative 1536-frame targets. Optional `buffer_frames=256..2048` overrides the hardware/local-listening targets for measured tuning; the remote playback minimum remains 4800 frames. Targets below the actual callback size can cause dropouts. A same-buffer simulated impulse test reduced local queue delay from 59.812 ms to 29.917 ms by removing one queue; this is not a measured AirPods latency.

Before the Wi-Fi buffering update, the paired CLI, using synthetic audio callbacks on this Mac, used approximately 2.2% (Windows role) and 3.1% (Mac role) of one CPU core, with about 6.8 MiB peak resident memory per process during a 20-second steady run. This includes codec, encrypted networking and routing, but excludes native drivers, GUI, Bluetooth and games. It does not establish performance on the target i7-7700 Windows PC.

Both interfaces reflect acknowledged controls. Failed audio streams retry; missing callbacks for two seconds trigger recovery, and macOS format/device changes invalidate streams for reopening. Recovery has automated fault-injection coverage but still requires physical unplug/sleep/Bluetooth testing. Remaining work includes filtered resampling, production signing, hardware latency measurements and performance tuning. The iPhone full audio-routing role is unsupported.


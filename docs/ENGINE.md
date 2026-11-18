# Engine reference

The v0.3 native interfaces use the room agent. This reference documents the
advanced standalone engine and the retained audio path. It is not the normal
room setup procedure. The new agent passes its derived media key through a
private inherited stdin pipe using `key_stdin=1`; legacy `key_id` and `key_file`
configuration remain supported for standalone use.

## Configuration

Run `san-audisco devices` on each computer to obtain stable device IDs. Create a UTF-8 file with these exact fields (no quotes around values):

```ini
slot=0
peer=192.168.1.20
port=45670
peer_port=45670
key_id=paired-peer
playback_input=ID_OF_CABLE_A_RECORDING_ENDPOINT
microphone_output=ID_OF_CABLE_B_PLAYBACK_ENDPOINT
headphones_output=ID_OF_AIRPODS_PLAYBACK_ENDPOINT
physical_microphone=ID_OF_PHYSICAL_MICROPHONE
```

The paired computer uses `slot=1` and its own native endpoint IDs. Slots are
assigned by the room agent from stable computer IDs, never from OS. The platform
backend is selected at build time. The agent generates this file under the legacy
private `room-v2` directory for upgrade compatibility. The two virtual cable fields
are required; physical output/microphone may be absent on a non-owning computer.

The engine is a child of the room agent, not a second settings coordinator. Its
stdin protocol accepts `quit`, `test_tone`, or a complete versioned settings frame:

```text
state 1 1 primary 0|mic_source 1|mute 1|deafen 0|balance 0|master 1|slot0_volume 1|slot0_mute 0|slot0_deafen 0|slot0_silence 0|slot1_volume 1|slot1_mute 0|slot1_deafen 0|slot1_silence 0|
```

The first two numbers are authority epoch and settings revision. Lower epochs
and non-increasing revisions are ignored. The room agent sends acknowledged
snapshots to both engines. Standalone diagnostic harnesses must supply snapshots
to both children; individual legacy `win_*`/`mac_*` commands no longer apply.

`mute` blocks microphone to both destinations. Per-participant mute blocks voice
only to that participant. `silence` blocks its playback contribution; `deafen`
blocks that participant's playback and microphone delivery without rewriting its
stored mute preference. Balance -1 favors slot0, +1 slot1. UI labels use actual
computer names. App controls do not alter Discord's own mute indicator or the
game's push-to-talk behavior.


## Architecture and implementation status

C++ engine: stereo float 48kHz buses, microphone mono fold-down, independent route gates, gain ramps, peak limiter, atomic settings snapshots. The audio callbacks use preallocated queues. Headphone playback mixes directly in its output callback, so the local listening path crosses one queue. A separate capture queue feeds the worker, which handles networking, codec, microphone routing and state replication. A bounded adaptive resampler corrects device clock differences. The current linear interpolation resampler is functional but needs a filtered replacement for optimal quality.

Native audio: macOS Core Audio HAL/AudioUnit; Windows WASAPI shared-mode event callbacks. GUI: SwiftUI on Mac and Win32 on Windows, communicating with the persistent room agent. Physical devices are explicitly selected. The agent temporarily routes OS defaults through the virtual endpoints when both engines are ready and restores them on Stop/disconnect, respecting manual overrides.

Transport: libsodium XChaCha20-Poly1305 with a pre-shared random key, fresh session epochs, recipient binding and anti-replay window. Opus stereo at 128kbps for playback and 48kbps for microphone, 5ms frames, complexity 3. Bounded jitter buffers reorder packets. The Output Hub agent coordinates settings over the authenticated room TCP channel. Media control packets acknowledge applied authority/revisions; they cannot change settings. See V03_ARCHITECTURE.md for handover and migration. Local-only LAN, numeric IPv4. The Windows worker uses its own high-resolution waitable timer for 5 ms deadlines; it does not change the system timer resolution.

Listening buffers automatically follow the larger capture/render callback hint plus 240 frames (5 ms), bounded to 256–4096 frames. If the device cannot report its callback size, the target is 1536 frames (32 ms). Remote playback has its own queue target of at least 4800 frames (100 ms), so packet jitter does not increase the local game buffer. Received microphone audio also has a 4800-frame reserve. The 32-packet reorder queues and remote reserves cover the observed 80 ms Wi-Fi bursts; they add remote delay without enlarging the local game queue. Network transmission and microphone hardware queues retain conservative 1536-frame targets. Optional `buffer_frames=256..2048` overrides the hardware/local-listening targets for measured tuning; the remote playback minimum remains 4800 frames. Targets below the actual callback size can cause dropouts. A same-buffer simulated impulse test reduced local queue delay from 59.812 ms to 29.917 ms by removing one queue; this is not a measured AirPods latency.

Before the Wi-Fi buffering update, the paired CLI, using synthetic audio callbacks on this Mac, used approximately 2.2% (Windows role) and 3.1% (Mac role) of one CPU core, with about 6.8 MiB peak resident memory per process during a 20-second steady run. This includes codec, encrypted networking and routing, but excludes native drivers, GUI, Bluetooth and games. It does not establish performance on the target i7-7700 Windows PC.

Both interfaces reflect acknowledged controls. Failed audio streams retry; missing callbacks for two seconds trigger recovery, and macOS format/device changes invalidate streams for reopening. Recovery has automated fault-injection coverage but still requires physical unplug/sleep/Bluetooth testing. Remaining work includes filtered resampling, production signing, hardware latency measurements and performance tuning. The iPhone full audio-routing role is unsupported.


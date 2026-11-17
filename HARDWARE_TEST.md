# Windows 10 / i7-7700 / AirPods Pro 3 acceptance run

Target provided by the user: Windows 10, Intel Core i7-7700, AirPods Pro 3; Discord on the MacBook Air. Native Windows software tests and silent stereo AirPods output have now passed on this PC. Complete two-machine audio routing and game performance remain pending. A Windows executable running under Wine does not reproduce the PC's Bluetooth or audio drivers.

## First configuration

Use Windows as primary and the MacBook built-in microphone as the microphone source. This keeps game audio local to the PC while Discord playback and the MacBook microphone cross the network. AudioBridge relays that one microphone to both virtual microphone destinations.

On Windows 10, select the AirPods **Stereo** output for headphones. Keep **Mic source = Mac**. Opening a Bluetooth hands-free microphone selects HFP, which uses mono voice audio and causes streams on the stereo endpoint to be discarded. This is a Windows/Bluetooth constraint, not a mixer setting. [Microsoft: Bluetooth Classic Audio](https://learn.microsoft.com/en-us/windows-hardware/drivers/bluetooth/bluetooth-classic-audio).

Leave the Windows physical microphone selector unused unless testing a PC microphone. On Mac select the MacBook microphone; the headphones selector can remain unused while Windows is primary. Physical device selection is explicit; there is no automatic fallback to a different microphone or speaker.

Apple documents non-Apple Bluetooth headset pairing for AirPods Pro 3: with the earbuds in the open case, double-tap its front until the light flashes white, then select it on the other device. For Mac usage, Apple's listed minimum for AirPods Pro 3 is macOS Tahoe, independently of AudioBridge's macOS 13 deployment target. [Apple: pairing and system requirements](https://support.apple.com/en-au/108786).

## Setup and routing

1. Install two independent virtual cables on each computer as described in README.md. Driver packages are separate; follow their normal installers and any license/admin prompts. No driver license purchase is required for the proposed BlackHole and VB-CABLE/Hi-Fi Cable paths.
2. Windows: Deadlock output → Cable A playback; Deadlock input → Cable B recording. AudioBridge playback input → Cable A recording; microphone output → Cable B playback; headphones → AirPods Stereo.
3. Mac: Discord output → Cable A playback; Discord input → Cable B recording. AudioBridge playback input → Cable A recording; microphone output → Cable B playback; physical microphone → MacBook microphone.
4. Put both computers on the same private LAN. Enter each computer's numeric IPv4 address in the other app. Default UDP port is 45670. Guest Wi-Fi isolation can block the pair.
5. Create one pairing key, transfer it privately, select it in both apps and Start. The apps import it into Keychain/DPAPI. Once both have imported it, the transfer files are no longer needed.
6. Confirm Connected. Begin at a comfortable output volume; explicitly unmute when ready to test voice. Reconnects and role changes intentionally require unmuting again.

## Functional evidence to collect

- With separate sources audible, move each volume and balance slider. Each source must respond independently; the microphone feed must contain voice alone.
- Test Windows mic mute, Mac mic mute and global mute independently using the receiving application's mic test. A route's deafen must stop its playback and voice; listening-only silence is available from the CLI or by lowering its volume.
- Test game push-to-talk separately: AudioBridge does not press its key or change Discord's mute/deafen icons.
- Stop/restart a peer, interrupt Wi-Fi, remove the AirPods and reconnect. Confirm fresh audio resumes without stale speech and reconnect remains muted until enabled.
- Move AirPods to Mac through Bluetooth settings. Stop the Mac app if needed to refresh/select its headphones, restart, then choose Mac as primary in the mixer. Verify Windows game audio crosses to Mac. Automatic Bluetooth handoff is not implemented.
- Test AirPods microphone only after the separate-mic baseline passes. On Windows 10, choose compatible hands-free playback/capture routes and expect voice-profile quality. Returning to stereo requires closing hands-free capture. The app cannot bypass these OS profile rules.

## Performance comparison

Use the same Deadlock scene, resolution, settings and network. Record 5-minute runs for game alone, game + Discord on PC, and game + AudioBridge on PC with Discord on Mac. Record median FPS, 1% lows/frame-time percentiles, process CPU and RAM, dropouts, and perceived audio delay. Include both AudioBridgeApp.exe and audiobridge.exe in the app resource total. Keep measurement tools and background tasks identical across runs.

Proposed acceptance targets, to confirm against the actual experience: no unintended voice on muted routes; no sustained dropouts during a two-hour run; no more than 2% median-FPS regression or 5% regression in 1% lows versus game alone; added local game latency no more than 20 ms versus direct playback through the same AirPods. These are targets, not measured results. Bluetooth's baseline delay is present in both comparisons.

Measure latency using a repeatable signal and correlated recordings rather than judging the software's queue size as total latency. Separate local game delay from remote Discord/voice delay. If a target fails, retain logs, callback formats and the comparison settings before tuning buffer_frames; shrinking a buffer below its device callback quantum can produce silence/dropouts.

## Current result

**Partial hardware validation passed.** All 15 native Windows test groups passed, including protected pairing, synthetic routing, silent AirPods stereo output and injected WASAPI timeout/reopen recovery. Before the Wi-Fi buffer update, the synthetic Windows-role engine measured 1.556% of one logical core and 7.160 MiB working set on the i7-7700 over 10.043 steady seconds; this excludes native audio, Bluetooth, GUI and game load. No Deadlock FPS, audible AirPods latency or complete two-machine hardware result is claimed. See `validation/windows-native-2026-11-17.log` and VALIDATION.md.

The updated synthetic pair passed all 16 continuous routing/control phases on both physical computers over Wi-Fi. An additional deterministic test covers repeated 80 ms receive gaps. Remote playback and microphone reserves are now 100 ms; local game buffering is unchanged. These passes do not replace physical latency, Discord, game or soak measurements.

The production rebuild subsequently passed all 15 native Windows groups again with Deadlock left running in the background. The short CPU counters returned zero ticks and are not a valid zero-cost estimate; synthetic working sets were approximately 7.7 MiB per role. See validation/windows-native-v2/.

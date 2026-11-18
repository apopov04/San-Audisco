# San Audisco v0.2.9

San Audisco (formerly AudioBridge) mixes audio from a Windows PC and a Mac into headphones connected to
one computer, and sends a selected physical microphone to voice apps on both.
The **Output Hub** plays the mixed audio through its selected output. The
**Input Hub** supplies its selected microphone to both computers. These roles
are independent and can belong to the same computer or different computers.

This private release adds rooms, synchronized setup and controls, background
operation, and installers containing the existing audio components. It retains
the working audio transport and Wi-Fi buffer settings. The previous version was
used successfully with Discord on the Mac and Deadlock on Windows; this new
release has automated and simulated validation, with installation and listening
acceptance still required on the actual pair. See [v0.2 validation](docs/V02_VALIDATION.md).

Version 0.2.9 adds automatic system playback routing and restoration, and reduces
Windows redraws. See [routing and update notes](docs/V029_UPDATE.md) and
[signing and OS permissions](docs/SIGNING.md).

## Start using a room

1. Quit the old AudioBridge app on both computers. Keep its binaries for rollback.
   Run the matching **AudioBridge Setup** privately supplied for each computer.
   Existing usable drivers are retained. Follow any normal vendor/OS prompts;
   restart only when convenient. Reopen setup after restarting.
2. Open AudioBridge on both computers, on the same private Wi-Fi/LAN. Create a
   room on either and enter its eight-character code on the other. Invitations
   expire after five minutes. Pairing is remembered; no key files or normal IP
   entry are required. If broadcast discovery fails, use the creator's IPv4
   address in the optional address field.
3. For the current setup, select **Windows PC as Output Hub** with **AirPods stereo**
   as its Output, and **MacBook as Input Hub** with its **built-in microphone**
   as its Input. Virtual routes are detected automatically where unambiguous. Device
   selections for either computer can be changed from either interface.
4. Configure Discord and Deadlock once using the table below. Grant the Mac's
   microphone/local-network permissions and the Windows private-network firewall
   prompt locally. OS permission dialogs cannot be approved from the other app.
5. Press **Autoconfigure** on either computer to configure both devices and enable
   automatic system input/output selection. Explicit app device selections are
   respected. Start audio, check the connection/readiness and signal meters, then explicitly
   unmute. Each device has a quiet two-second test sound button. Test sounds are
   only generated on request and enter the listening bus, never the mic bus.

Once both audio engines are ready, the app switches each computer's default
playback to its playback cable. Apps following the system default (including a
normal browser) then enter the mix automatically. Stop audio or Quit restores
the outputs saved before that session. A manual output change during a session
is respected. Explicit app output selections still take precedence.

| Application | Output / speaker | Input / microphone |
|---|---|---|
| Discord on Mac | BlackHole 2ch | BlackHole 16ch |
| Deadlock on Windows | CABLE Input (VB-Audio Virtual Cable) | Hi-Fi Cable Output |

The app captures the opposite end of each playback cable and feeds the opposite
end of each microphone cable. Keep the playback and microphone cables separate.
Do not enable Windows “Listen to this device.” Hi-Fi Cable's playback and recording
endpoints must both use stereo 48,000 Hz; setup detects mismatches and guides the
required Sound Settings step. AirPods remain paired through OS Bluetooth settings.
Selecting a new Output Hub does not automatically move their Bluetooth connection.

## Controls and background operation

Both interfaces open with a two-computer overview. Blue indicates an available
computer; gray indicates offline or unavailable, with matching text labels. The
line between them shows room connectivity, separately from whether audio is running.

Choose **Input** and **Output** side by side beneath each computer. Above its
icon, **Use as Input Hub** chooses the shared microphone source and **Use as
Output Hub** chooses where you listen. Active roles show **Input Hub** or
**Output Hub**; selecting one does not change the other.
Physical selectors exclude virtual cables. Each computer has its own volume,
microphone mute and deafen controls. Overall volume and global mute/deafen stay
within reach. Room pairing, audio checks and advanced cables are secondary controls.
Balance is available in Room settings on Mac and Advanced on Windows.

- **Mute** stops microphone delivery. It is available globally and per destination.
- Set a device's volume to **0%** to remove it from the headphone mix without
  muting its microphone.
- **Deafen** stops listening and microphone delivery globally or for that device.
- **Stop audio** stops both engines when both devices are reachable. Closing the
  window keeps audio running; use the menu-bar/notification-area icon to reopen it.
- **Quit AudioBridge** stops the app and agent on that computer. Optional login
  startup is controlled locally on each computer. No login item is enabled by default.

Startup, reconnect, and audio role changes protectively mute the microphone. The
interface explains this; unmute after readiness is restored. The app's controls
operate the audio routes and do not change Discord's mute/deafen icons or bypass
game push-to-talk. Signal meters show levels, not recordings; nothing records or
uploads microphone content.

## Setup, compatibility, and recovery

See [private setup and rollback](PRIVATE_SETUP.md) for packaging, driver prompts,
restarts, removal, and the exact third-party component versions. One DMG on Mac
and one setup EXE on Windows include the required original vendor packages.
Installation is not completely silent: normal vendor interfaces, administrator
approval, permissions, and a possible driver restart remain.

- Windows x64: Windows 10 1803+ or Windows 11. Target PC: Windows 10 build 19045,
  i7-7700, AirPods Pro 3.
- Mac: Apple Silicon, macOS 13+. Native execution validated on this macOS 26 host;
  the older minimum OS still needs execution testing.
- One Mac and one Windows computer on a LAN. No accounts, cloud audio, Internet
  relay, multi-member rooms, or iPhone audio-routing role in this release.
- Mac app/setup are ad-hoc development signed; Windows app/setup are unsigned.
  Production notarization/signing was not supplied. Keep normal OS security enabled.

Existing `mac.conf` / `windows.conf` route choices are copied into the separate
`room-v2` state directory on first launch, with a legacy backup. Old files and
credentials are retained for rollback. You still create the new room once.
Application settings live in the user's AudioBridge application-data directory;
room secrets use macOS Keychain or current-user Windows DPAPI.

If sound stops, check which stage has signal: source playback, selected microphone,
and delivered microphone. Check the displayed protective mute reason and device
readiness before unmuting. Refresh devices after reconnecting headphones. For
pairing failures check the same network, local-network permission, and private
firewall access for `bridge-agent` (UDP/TCP 45671) and the engine (UDP 45670).
Guest-network client isolation can prevent connection.

## Build from source

Build tools are needed only on the build machine: C++20, CMake 3.20+, Python 3,
libsodium 1.0.20, Opus 1.5.2, and the pinned BoringSSL crypto library. Mac UI builds
also require Swift/Xcode command-line tools. The JSON header is vendored with its
MIT notice. BoringSSL and the audio libraries are statically linked.

```sh
./scripts/bootstrap-deps.sh /absolute/path/to/audio-deps
python3 scripts/bootstrap-room-crypto.py /absolute/path/to/room-deps
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DSODIUM_ROOT=/absolute/path/to/audio-deps \
  -DBORINGSSL_ROOT=/absolute/path/to/room-deps
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./scripts/package-mac.sh "$PWD/build" /absolute/path/to/candidate/AudioBridge.app
```

For Windows cross-builds, build the dependencies for Windows and use
`scripts/build-windows-cross.sh` with `BORINGSSL_ROOT` pointing to the Windows crypto
prefix. `.github/workflows/windows.yml` provides a native MSVC CI recipe. Set
`-DBRIDGE_BUILD_ROOMS=OFF` only for engine-only development builds. Test binaries,
especially `runtime_fake`, are never application/installer payloads.

For private installer assembly use `scripts/package-private.py` as documented in
[PRIVATE_SETUP.md](PRIVATE_SETUP.md). Vendor packages live outside the source tree
and are hash-pinned by `installer/vendor-lock.json`. Do not publish those private
artifacts or infer redistribution rights from personal use.

## Technical references

- [System architecture and state ownership](docs/ARCHITECTURE.md)
- [Room protocol, cryptography, and boundaries](docs/ROOM_PROTOCOL.md)
- [v0.2 validation and hardware acceptance](docs/V02_VALIDATION.md)
- [Engine CLI and latency characteristics](docs/ENGINE.md)
- [Historical v0.1 test evidence](VALIDATION.md)
- [Target hardware test procedure](HARDWARE_TEST.md)

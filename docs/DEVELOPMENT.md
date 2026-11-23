# Development and detailed operation

San Audisco's own source is available under the [MIT license](../LICENSE).
Third-party libraries and separately installed audio drivers retain their own
licenses; see [distribution](DISTRIBUTION.md) before bundling vendor installers.

San Audisco (formerly AudioBridge) mixes audio from two Windows or Mac computers through headphones or speakers connected to
one computer, and sends a selected physical microphone to voice apps on both.
The **Output Hub** plays the mixed audio through its selected output. The
**Input Hub** supplies its selected microphone to both computers. These roles
are independent and can belong to the same computer or different computers.

This first GitHub preview release retains the working two-computer audio path and
adds code cleanup, consistent release metadata, and current setup documentation.
The source repository is public; bundled-driver installers remain private. This is not a signed,
notarized, or store-approved public release. See [distribution options](DISTRIBUTION.md)
for the distinction between an app-only download and a complete private setup package.

The owner has used the preceding v0.3.3 release with Discord/video on the Mac and
Deadlock on Windows, and with the MacBook speakers playing PC audio. These are
successful listening reports, not measured Bluetooth latency, game benchmarks,
or a quantified reliability guarantee. Historical test results remain in the
version-specific validation documents; they do not automatically validate this cleanup.

## Current behavior

- Either computer can be the Output Hub or Input Hub, regardless of operating system.
- Both interfaces use the same device overview and synchronized controls.
- Autoconfigure enables automatic system input/output routing; Stop and Quit restore
  previous defaults where possible and preserve manual overrides.
- Quit stops the shared session and exits locally even when a physical output is
  unavailable. Recovery information is retained and reopening remains paired with
  audio stopped. See [Quit recovery](V033_QUIT.md).
- Windows uses native controls with System, Light, or Dark appearance. Resize
  painting and the bridge/status layout include the fixes described in
  [Windows appearance](WINDOWS_APPEARANCE.md) and [resize fixes](V032_FIXES.md).

## Start using a room

1. Install the matching build on each computer. The private **San Audisco Setup**
   packages include dependency checks and the existing vendor installers. App-only
   downloads require the separate components described in
   [installation and distribution](DISTRIBUTION.md). Quit San Audisco before
   updating. Existing usable drivers are retained; follow normal vendor/OS prompts
   and restart when convenient if requested.
2. Open San Audisco on both computers, on the same private Wi-Fi/LAN. Create a
   room on either and enter its eight-character code on the other. Invitations
   expire after five minutes. Pairing is remembered; no key files or normal IP
   entry are required. If broadcast discovery fails, use the creator's IPv4
   address in the optional address field.
3. Choose an **Output Hub** and its physical Output, then an **Input Hub** and
   its microphone. For example, listen through AirPods connected to Windows while
   using the MacBook microphone. Virtual routes are detected automatically where
   unambiguous. Device selections for either computer can be changed from either interface.
4. Configure application device overrides if needed; the table below is an example. Grant the Mac's
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
the outputs saved before that session (or the selected physical devices when
recovering an older cable selection with no saved record). A manual output change during a session
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
bridge between them shows room connectivity, separately from whether audio is running.

Choose **Input** and **Output** side by side beneath each computer. Above its
icon, **Use as Input Hub** chooses the shared microphone source and **Use as
Output Hub** chooses where you listen. Active roles show **Input Hub** or
**Output Hub**; selecting one does not change the other.
Physical selectors exclude virtual cables. Each computer has its own volume,
microphone mute and deafen controls. Overall volume and global mute/deafen stay
within reach. Room pairing, audio checks and advanced cables are secondary controls.
Balance is available in Room settings on Mac and Advanced on Windows.

- **Mute** stops microphone delivery. It is available globally and per destination.
- Set a device's volume to **0%** to remove it from the listening mix without
  muting its microphone.
- **Deafen** stops listening and microphone delivery globally or for that device.
- **Stop audio** stops both engines when both devices are reachable. Closing the
  window keeps audio running; use the menu-bar/notification-area icon to reopen it.
- **Quit San Audisco** stops the shared audio session and closes the app/agent on
  that computer; the other app stays open. Optional login
  startup is controlled locally on each computer. No login item is enabled by default.

Startup, reconnect, and audio role changes protectively mute the microphone. The
interface explains this; unmute after readiness is restored. The app's controls
operate the audio routes and do not change Discord's mute/deafen icons or bypass
game push-to-talk. Signal meters show levels, not recordings; nothing records or
uploads microphone content.

## Setup, compatibility, and recovery

See [private setup and rollback](../PRIVATE_SETUP.md) for packaging, driver prompts,
restarts, removal, and the exact third-party component versions. One DMG on Mac
and one setup EXE on Windows include the required original vendor packages.
Installation is not completely silent: normal vendor interfaces, administrator
approval, permissions, and a possible driver restart remain.

- Windows x64: Windows 10 1803+ or Windows 11. Target PC: Windows 10 build 19045,
  i7-7700, AirPods Pro 3.
- Mac: Apple Silicon, macOS 13+. Native execution validated on this macOS 26 host;
  the older minimum OS still needs execution testing.
- Two computers on a LAN: Windows–Windows, Mac–Mac or Windows–Mac. No accounts, cloud audio, Internet
  relay, multi-member rooms, or iPhone audio-routing role in this release.
- Mac app/setup are ad-hoc development signed; Windows app/setup are unsigned.
  Production notarization/signing was not supplied. Keep normal OS security enabled.

Existing `mac.conf` / `windows.conf` route choices are copied into the separate
`room-v2` state directory on first launch, with a legacy backup. Old files and
credentials are retained for rollback. Existing v0.2 rooms migrate without re-pairing; new users create a room once.
Application settings live in the user's legacy AudioBridge application-data directory;
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
./scripts/package-mac.sh "$PWD/build" "/absolute/path/to/candidate/San Audisco.app"
```

For Windows cross-builds, build the dependencies for Windows and use
`scripts/build-windows-cross.sh` with `BORINGSSL_ROOT` pointing to the Windows crypto
prefix. `.github/workflows/windows.yml` provides a native MSVC CI recipe. Set
`-DBRIDGE_BUILD_ROOMS=OFF` only for engine-only development builds. Test binaries,
especially `runtime_fake`, are never application/installer payloads.

For private installer assembly use `scripts/package-private.py` as documented in
[PRIVATE_SETUP.md](../PRIVATE_SETUP.md). Vendor packages live outside the source tree
and are hash-pinned by `installer/vendor-lock.json`. Do not publish those private
artifacts or infer redistribution rights from personal use.

## Technical references

- [v0.4.0 cleanup and verification scope](V040_RELEASE.md)
- [System architecture and state ownership](ARCHITECTURE.md)
- [Room protocol, cryptography, and boundaries](ROOM_PROTOCOL.md)
- [v0.2 validation and hardware acceptance](V02_VALIDATION.md)
- [Engine CLI and latency characteristics](ENGINE.md)
- [Historical v0.1 test evidence](../VALIDATION.md)
- [Target hardware test procedure](../HARDWARE_TEST.md)

## Device-role architecture

Windows–Windows, Mac–Mac and mixed rooms use device IDs and an Output Hub coordinator. Input Hub is independent. Both devices must support application protocol 3 (introduced in v0.3); use matching app releases. Existing rooms and routes migrate without re-pairing. See [v0.3 architecture](V03_ARCHITECTURE.md) for protocol and migration, and [v0.3 validation](V03_VALIDATION.md) for historical completed checks and their hardware-validation limits.

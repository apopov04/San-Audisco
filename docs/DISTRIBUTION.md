# Downloads, installation, and distribution

San Audisco v0.4.0 is a first GitHub **preview**, not a store release or a
certified installer. The repository is private. Packaging does not change its
visibility or grant rights to redistribute third-party software.

## Choose the appropriate package

| Package | Includes | Dependency setup |
|---|---|---|
| macOS app-only ZIP | San Audisco.app, notices, documentation | Install BlackHole 2ch and 16ch separately if absent. |
| Windows app-only ZIP | GUI, agent, audio engine, notices, documentation | Install VB-CABLE and Hi-Fi Cable separately if absent. |
| Private setup DMG / EXE | App, dependency checks, original vendor installers | Guided installation for the owner's machines; not a public release asset. |
| Source ZIP | Tracked source and notices | Build tools and libraries are required to compile it. |

A single app download is sufficient on a computer with the required components
already installed. On a new computer, the app-only preview does **not** install
the audio drivers. Autoconfigure selects available routes; it does not download
or install missing drivers.

The complete private wrappers can install missing components after checking the
machine. They still use normal vendor installation windows, administrator
approval, operating-system permissions, and a possible restart. See
[private setup](../PRIVATE_SETUP.md) for those packages. They remain local and
must not be attached to the GitHub preview release.

## Install the app-only preview

Quit San Audisco before replacing an existing app. Keep its settings, room data,
and installed audio drivers. Use matching app versions on both computers.

### macOS (Apple Silicon)

1. Obtain BlackHole **2ch and 16ch** from the
   [official BlackHole download page](https://existential.audio/blackhole/) if
   they are missing. Follow the vendor's installer and restart instructions.
2. Extract `San Audisco-macOS-arm64.zip`. Put `San Audisco.app` in
   `~/Applications/`, replacing the prior app only after it has quit.
3. Open the app. Review any Gatekeeper message and grant microphone/local-network
   permissions when requested. This preview is ad-hoc signed and **not notarized**;
   do not disable system security as a workaround. See [signing](SIGNING.md).

### Windows (x64)

1. Obtain [VB-CABLE](https://vb-audio.com/Cable/) and
   [Hi-Fi Cable](https://vb-audio.com/Cable/index.htm#DownloadASIOBridge) from their
   official product pages if absent. Follow vendor instructions and any restart
   request. Existing usable drivers do not need to be reinstalled.
2. Extract **all** files from `San Audisco-Windows-x64.zip` into a persistent
   folder, preferably `%LOCALAPPDATA%\SanAudisco\app`. Keep the GUI, agent, and
   engine together; do not run the application from inside the ZIP.
3. Launch `SanAudisco.exe`. This app-only archive does not create a Start Menu
   shortcut. Its executables are unsigned, so Windows may show a publisher or
   reputation warning. Review it without disabling Windows security.
4. Allow the agent and engine on your trusted **Private** network when prompted.
   Verify both Hi-Fi Cable endpoints are stereo **48 kHz** in Sound Settings.

Driver versions used for the owner's baseline are recorded in
`installer/vendor-lock.json`: BlackHole 0.7.1, VB-CABLE Pack45, and Hi-Fi Cable
setup 1.0.0.7. A newer vendor download is not automatically validated by these
historical tests; review compatibility before changing an existing working setup.

On both computers, open San Audisco, create/join a room, select the Input and
Output Hubs, and run Autoconfigure. Start audio and explicitly unmute when ready.
Applications with their own device overrides may need the choices shown in
[README](../README.md).

## Assemble a release without private vendor payloads

`scripts/package-release.py` consumes a build directory containing
`San Audisco.app` and a `windows/` directory with the three production executables.
It creates exactly these artifacts:

- `San Audisco-macOS-arm64.zip`
- `San Audisco-Windows-x64.zip`
- `San Audisco-source.zip`
- `BUILD_INFO.json`
- `SHA256SUMS.txt`

The version and Mac build number come from `CMakeLists.txt`; the packaged Mac
bundle must match. The source tree must be committed. `--allow-dirty` is a
development-only escape hatch and is recorded in the manifest, not a release
validation claim. Existing artifacts are not overwritten. Only the explicit
generated files enter the checksums; unrelated ZIPs in the directory are excluded.

```sh
python3 scripts/package-release.py --dist /absolute/path/to/release-builds
```

Review the actual archive contents and execute the intended tests before tagging
or uploading. Upload only the five named artifacts, never a directory wildcard.
Packaging records the source revision and executable hashes but does not prove
that binaries were built from that revision, nor does it execute audio tests.

## Vendor redistribution boundaries

Primary sources checked November 23, 2026:

- [VB-Audio general terms](https://shop.vb-audio.com/en/content/3-Terms-of-use),
  sections 3.1 and 3.4, distinguish personal use from distributor licenses and
  prohibit making the applications publicly available under the ordinary user
  license. [Donationware](https://vb-audio.com/Services/licensing.htm) does not
  mean unrestricted redistribution. Obtain appropriate vendor permission before
  publishing a combined installer.
- [BlackHole's current license](https://github.com/ExistentialAudio/BlackHole/blob/master/LICENSE)
  distinguishes GPLv3 source from official compiled binaries/installers and
  branding. The [project README](https://github.com/ExistentialAudio/BlackHole)
  also describes licensing for non-GPL projects. Do not infer permission to
  republish the official signed installer from source availability. Review the
  applicable package terms or obtain permission before public bundling.

The app-only route lets users obtain those components directly from their
vendors. Third-party library notices remain in the app packages. San Audisco's
own source-license choice is separate from those dependencies; repository access
alone is not a statement that the project is open source.

Private repository access is not a substitute for redistribution permission.
The local full bundles were made for the owner's machines; sharing them with
other users needs its own licensing review.

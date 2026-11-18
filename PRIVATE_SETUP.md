# AudioBridge private setup (v0.2)

These installers are assembled for the owner's private machines. They contain
third-party packages, are not public release artifacts, and must not be uploaded
to GitHub Releases or included in the source archive. Personal use is not a grant
of public redistribution rights. Vendor installers, signatures, copyright notices,
and licensing screens are preserved unchanged.

## What setup does

Windows setup installs the three application executables for the current user in
`%LOCALAPPDATA%\AudioBridge\app` and adds an AudioBridge Start Menu shortcut. Driver
installation opens the original VB-Audio installers with the normal Windows
administrator prompt. App installation does not require administrator rights.

Mac setup installs the complete application in `~/Applications/AudioBridge.app`.
The original, signed BlackHole installers install the audio components through
Apple Installer. They may request a restart. Setup never restarts the computer,
kills an audio application, silently changes system defaults, or alters system
security settings.

Both setup apps inspect the current audio endpoints. A vendor installer exiting
successfully is not treated as proof of functional audio. Existing components are
preserved. Disabled, stale, older, or incomplete installations require review;
setup does not run a vendor program blindly as a repair or removal operation.

Windows verifies both Hi-Fi Cable endpoints are stereo and 48 kHz before calling
them ready. If necessary, use **Sound settings (48 kHz)**, open **Playback → Hi-Fi
Cable Input → Properties → Advanced**, and choose a 48,000 Hz stereo format. Repeat
under **Recording → Hi-Fi Cable Output**. The setting must match on both sides.
No undocumented Windows policy COM interface is used to set this globally.

When the setup app is reopened after a restart, it re-inspects actual devices;
the state file is informational and does not suppress real verification. Progress
is recorded as `AudioBridge/setup-v2.json` in the user's application support/data
directory. If vendor installation was cancelled, retry its button. If a component
exists but is unavailable, review it or restart when convenient before retrying.

Closing setup does not start AudioBridge. **Open AudioBridge** is an explicit
action enabled only once required endpoints and the app are present. Microphone
and local-network permission prompts belong to the operating system and still
must be approved on the respective computer. First room setup configures the app's
virtual routes; Discord and Deadlock retain their own device selections.

## Windows private-network access

On your trusted home network, allow **bridge-agent.exe** and **audiobridge.exe**
through Windows Defender Firewall for **Private** networks when Windows prompts.
The installed binaries are in `%LOCALAPPDATA%\AudioBridge\app`. Do not enable
Public access or disable the firewall. If the prompt was previously declined, use
**Windows Security → Firewall & network protection → Allow an app through firewall**
and review those two installed programs. Administrator approval may be necessary.
Setup offers **Firewall help** but does not elevate or change firewall rules.

Room discovery uses UDP 45671; authenticated room control/pairing uses TCP 45671;
audio uses UDP 45670. The local GUI-to-agent port binds only to loopback and does
not need a LAN firewall rule. Both computers must be on a network that permits
peer-to-peer traffic; guest/client isolation may block pairing even with correct
firewall rules. Replacing or moving binaries can require reviewing their rules.
On Mac, approve the app's local-network permission if macOS asks.

## Packaging inputs

`installer/vendor-lock.json` pins the exact package versions, sizes, hashes, and
official source locations. Package files live outside the repository. The current
Windows baseline intentionally uses Pack45, matching prior testing, instead of
silently upgrading to the newer Pack46.

- `VBCABLE_Driver_Pack45.zip`, driver 3.3.1.7 (October 2024).
- `HiFiCableAsioBridgeSetup_v1007.zip`, setup 1.0.0.7.
- `BlackHole2ch-0.7.1.pkg` and `BlackHole16ch-0.7.1.pkg`, both signed by Existential
  Audio Inc., Apple developer team `Q5C99V536K`.

The Windows archive contents are embedded byte-for-byte and checked with SHA-256
before extraction. Before elevation, Windows verifies Authenticode trust and the
expected VB-Audio publisher on the vendor installer. An unavailable certificate
chain can stop installation; setup never installs a trusted publisher certificate
or disables signature checks to work around that. Mac verifies the package hashes,
Apple signature/publisher, and AudioBridge's bundle signature before enabling
installation. The private wrapper itself is development signed on Mac and unsigned
on Windows; it is not a trusted, publicly notarized/signed distribution.

Vendor documentation confirms interactive installation and restart requirements.
No unverified `-i`, `-h`, `/S`, or equivalent silent command is used. This release
removes separate downloads and extraction, while retaining the vendor installation
windows. Silent installation can be added only after its documented contract and
return codes are validated for these exact versions.

Build tools are needed only on the build machine, never on the end user's machines.
Supply a built v0.2 app containing all three binaries (including `bridge-agent`).

```sh
python3 scripts/package-private.py --platform mac \
  --app /absolute/private/build/AudioBridge.app \
  --vendor-dir /absolute/private/drivers --output /absolute/private/installers/mac

python3 scripts/package-private.py --platform windows \
  --app /absolute/private/build/windows \
  --vendor-dir /absolute/private/drivers --output /absolute/private/installers/windows \
  --toolchain /absolute/llvm-mingw/bin
```

The scripts do not install anything or launch audio. Mac output is a DMG containing
**AudioBridge Setup.app** and these instructions. Windows output is one native
bootstrap EXE. Hash sidecars identify the final artifacts. Existing output artifacts
are never overwritten. The build rejects missing/mismatched vendor packages and
unsafe ZIP members. Vendor versions can only be changed deliberately in the lock
file after validation; the installer never auto-downloads a new version.

## Update, uninstall, and rollback

Quit AudioBridge and its background agent before replacing application binaries.
Setup refuses to interrupt a running engine. Previous app directories are retained
as timestamp/UUID-named backups. Existing room credentials and configuration are
not rewritten by the installer.

To remove the Mac app, quit it and move `~/Applications/AudioBridge.app` to Trash.
On Windows, quit the app and agent and choose **Remove app only** in setup. This
removes the known application files and AudioBridge Start Menu shortcut; it keeps
unknown extra files rather than deleting an entire directory blindly. Leave the surrounding AudioBridge data
folder if you want to keep pairing/settings. Review backup app folders separately.
Removing AudioBridge never removes BlackHole or VB-Audio drivers: other programs
may depend on them. Vendor driver removal remains a separate explicit operation.

For application rollback, quit the agent and restore the retained previous app
directory. The known working source baseline is commit `32ae9cc`. Keep its original
application archives available until v0.2 real-device acceptance is complete.

## Validation boundaries

Packaging tests verify pinned inputs, hostile archive rejection, and artifact
structure. A Windows `--check-payload` invocation validates embedded hashes without
extracting/installing anything or requesting elevation. It is not an audio test.
Native compilation and package verification are not substitutes for clean-machine
driver installation, administrator-prompt, reboot-resume, or actual audio tests.
Those must be recorded separately; never infer successful installation from a
cross-build, VM without Bluetooth, or synthetic signal test.

Official references checked on 2026-11-18:

- https://vb-audio.com/Cable/
- https://vb-audio.com/Cable/VBCABLE_ReferenceManual.pdf
- https://vb-audio.com/Services/licensing.htm
- https://shop.vb-audio.com/en/content/3-Terms-of-use
- https://github.com/ExistentialAudio/BlackHole/blob/master/LICENSE
- https://github.com/ExistentialAudio/BlackHole#installation-instructions

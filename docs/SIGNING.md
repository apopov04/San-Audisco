# Signing and access prompts

These private builds are ad-hoc signed unless `SAN_AUDISCO_SIGNING_IDENTITY`
names a valid Developer ID Application identity in the build machine's Keychain.
Changing an ad-hoc binary changes its code identity. An update can therefore
require microphone, local-network, firewall, or Keychain access again. A stable
installation path alone does not establish a stable signing identity.

`scripts/package-mac.sh` signs the engine and agent explicitly, then the app.
`scripts/package-private.py` signs the setup app using the same configured
identity. Certificate-backed builds use hardened runtime, secure timestamps,
and the audio-input entitlement for the audio executables and application.
Keep the existing bundle identifier and the same signing identity across updates.
No identity is created or certificate trust settings changed by these scripts.

For distribution, obtain/configure Developer ID first. Then build with:

```sh
export SAN_AUDISCO_SIGNING_IDENTITY='Developer ID Application: Your Name (TEAMID)'
./scripts/package-mac.sh /path/to/build /path/to/San Audisco.app
```

Package the signed app with `scripts/package-private.py`. With notarization
credentials already stored in a Keychain profile, submit the final DMG:

```sh
xcrun notarytool submit /path/to/setup.dmg --keychain-profile PROFILE --wait
xcrun stapler staple /path/to/setup.dmg
xcrun stapler validate /path/to/setup.dmg
spctl --assess --type open --context context:primary-signature --verbose /path/to/setup.dmg
```

Only report notarization complete after Apple accepts it and stapling validates.
Regenerate the artifact's SHA-256 after stapling. Do not put account passwords,
certificate private keys, or notarization secrets into source or build logs.

Notarization addresses Gatekeeper distribution checks; it does not pre-grant
microphone, local-network or Keychain access. The user must grant those OS
permissions. Existing Keychain items created by an older ad-hoc build may need
one further approval after moving to a persistent signing identity. Do not weaken
their ACLs or store the room keys in plaintext to hide a prompt.

Windows executable signing is a separate Authenticode certificate workflow.
Unsigned development upgrades can still trigger SmartScreen/firewall prompts.
The app runs unelevated; installing missing audio drivers or firewall rules may
need administrator consent. Existing healthy drivers are retained by Setup.

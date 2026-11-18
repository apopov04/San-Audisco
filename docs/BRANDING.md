# San Audisco branding and upgrade compatibility

The app, Dock/Finder bundle, Windows executable metadata, taskbar/tray icon,
Start Menu shortcut, setup applications, help text and current source names use
San Audisco. The approved red/yellow artwork is in `assets/` with generated
macOS ICNS and Windows ICO containers. `scripts/build-icons.py` regenerates them
without changing the artwork. These are native packaged icons; they are not an
Icon Composer document.

Mac installs to `~/Applications/San Audisco.app`. Windows installs to
`%LOCALAPPDATA%\SanAudisco\app` and creates `San Audisco.lnk`. The installers
recognize the previous executable/bundle, retain a rollback copy, and retire the
known old launcher. Drivers are preserved. Windows login registration is migrated
only when its value points to the known previous app.

The following legacy strings deliberately remain for upgrade compatibility:

- `AudioBridge` application-data directories contain existing room/configuration,
  DPAPI credentials and driver-setup state. Renaming visible apps does not move or
  discard that data.
- `local.audiobridge.*` bundle/signing identifiers preserve the OS application
  identity. `org.audiobridge.pairing` Keychain service and Windows DPAPI entropy
  must match the existing encrypted room credentials.
- The room protocol domain-separation string remains byte-for-byte unchanged.
- Old executable/shortcut/window-class names occur only in upgrade detection,
  singleton protection, and migration code. The setup mutex is shared with old
  installers so two versions cannot install concurrently.
- Historical validation evidence records the name used at the time.

Do not globally replace these compatibility identifiers: doing so would break
existing pairings, credentials or upgrade detection. Ad-hoc signed builds can
still require OS access approval after a binary update; branding cannot suppress
those checks.

# Native room UI validation

These tests exercise the production GUI/model against an explicitly test-only agent protocol fixture. The fixture performs no pairing, audio capture, playback, driver installation, or credential access. These checks do **not** establish real daemon, Bluetooth, hardware, or installer behavior; those require the separate integration and hardware suites.

## macOS

Run `python3 tests/ui/mac_room_model_test.py` on an Apple Silicon Mac with Xcode command-line tools. It compiles the production `BridgeModel` and tests its JSON state mapping and bounded subprocess IPC with a fixture. Temporary artifacts are removed afterward.

The model checks also cover device identities and kinds, same-platform peers, stale peer inventory versus actual connectivity, invitation cancellation, and input/output hub selection. They do not instantiate the SwiftUI views. For visual checks, use a separate test app bundle and the fixture executable, without replacing or launching the installed production app.

## Windows

Build `tests/ui/agent_fixture.cpp` as `bridge-agent.exe` with the vendored nlohmann include directory. Place it beside a test copy of `SanAudisco.exe` in a disposable directory. Run:

```
python tests/ui/windows_room_ui_test.py path/to/SanAudisco.exe
```

The app embeds the Windows common-controls v6 manifest and scales controls and graphics together for system DPI. Set `SAN_AUDISCO_UI_TEST=1` for a build compiled with `BRIDGE_UI_TEST`; this uses an isolated window class. An optional second command-line argument selects a test DPI (96, 120, 144, or 192), and `SAN_AUDISCO_UI_THEME=light` or `dark` forces a test appearance. Native display scaling still needs visual acceptance on Windows.

The test refuses to run if another room UI window exists. It uses standard Win32 messages to exercise the actual native interface: main device-column bounds, offline/online colors and labels, physical-only endpoint lists, direct remote route choices, invite creation, nearby room selection, invalid and formatted codes, acknowledged pairing/start, remote configuration, Hub selection, readiness display, local/remote signal meters, explicit test-sound routing, cancellation while waiting to restart, agent reconnection, close-to-tray, and explicit Quit.

Running under Wine is compatibility validation only, not native Windows validation. The fixture writes only `ui-fixture-state.json` and `ui-fixture-requests.jsonl` beside its own executable. Do not distribute the fixture renamed as `bridge-agent.exe` in a real release.

# Native room UI validation

These tests exercise the production GUI/model against an explicitly test-only agent protocol fixture. The fixture performs no pairing, audio capture, playback, driver installation, or credential access. These checks do **not** establish real daemon, Bluetooth, hardware, or installer behavior; those require the separate integration and hardware suites.

## macOS

Run `python3 tests/ui/mac_room_model_test.py` on an Apple Silicon Mac with Xcode command-line tools. It compiles the production `BridgeModel` and tests its JSON state mapping and bounded subprocess IPC with a fixture. Temporary artifacts are removed afterward.

The native SwiftUI candidate was additionally opened in an isolated `local.san-audisco.room-uitest` app bundle. The v0.2.1 device overview, connected/offline graphics, physical-only picker contents, direct Hub selection and room settings sheet were inspected via accessibility and screenshots. Model tests also cover stale peer inventory versus actual connectivity and invitation cancellation. Quit stopped that fixture app; the installed San Audisco app was not replaced.

## Windows

Build `tests/ui/agent_fixture.cpp` as `bridge-agent.exe` with the vendored nlohmann include directory. Place it beside a test copy of `SanAudisco.exe` in a disposable directory. Run:

```
python tests/ui/windows_room_ui_test.py path/to/SanAudisco.exe
```

The v0.2.1 build embeds the Windows common-controls v6 manifest and scales native controls and graphics together for system DPI. Native 150% hardware rendering still requires acceptance.

The test refuses to run if another room UI window exists. It uses standard Win32 messages to exercise the actual native interface: main device-column bounds, offline/online colors and labels, physical-only endpoint lists, direct remote route choices, invite creation, nearby room selection, invalid and formatted codes, acknowledged pairing/start, remote configuration, Hub selection, readiness display, local/remote signal meters, explicit test-sound routing, cancellation while waiting to restart, agent reconnection, close-to-tray, and explicit Quit.

Running under Wine is compatibility validation only, not native Windows validation. The fixture writes only `ui-fixture-state.json` and `ui-fixture-requests.jsonl` beside its own executable. Do not distribute the fixture renamed as `bridge-agent.exe` in a real release.

# v0.2.1 device overview

The main screen follows the two-computer diagram: Windows on the left, Mac on the
right, and a room-connection link between them. Available devices are blue;
offline/unavailable devices and a disconnected link are gray. Text states accompany
the colors. Room connectivity is separate from audio running or microphone readiness.

Headphones and Microphone selectors appear beneath each computer. Physical choices
exclude virtual cables; cable routes remain under Advanced. The Hub chooses where
the mixed audio is played; the microphone source chooses whose microphone serves
both computers. Per-device volume, mic mute and deafen remain next to that device.
Global controls stay visible. Changing the Hub still requires connecting headphones
through OS Bluetooth settings. No automatic Bluetooth switching is implied.

macOS uses native SwiftUI controls and SF Symbols with secondary room/check sheets.
Windows uses themed Win32 controls (common-controls v6 manifest), Segoe UI, vector
desktop/laptop graphics and a default Audio overview; Room, Advanced and Checks are
secondary tabs. Its controls/fonts/graphics use consistent system-DPI scaling,
limited to fit the work area. Per-monitor DPI transitions are not implemented.

## Validation

- macOS production build and model/IPC fixture tests passed, including physical
  device filtering, cached peer versus actual connection state, invitation cancel,
  and existing synchronization behavior.
- Native Mac fixture UI was visually inspected in connected and disconnected
  states. Physical headphone menu contents, direct Hub selection, room settings
  and pinned global controls were checked. The fixture does not access real audio.
- Windows strict cross-build with the v6 manifest passed. Actual Win32 GUI fixture
  tests under Wine passed for main-control visibility/bounds, both device routes,
  physical-only dropdowns, remote configuration, roles, disconnection, failed status
  and unavailable-agent recovery, and existing room/background controls.
- Package-input tests passed (7 cases), and the Mac application signature verified.
- The audio engine, room agent/protocol and installed drivers are unchanged.

Native Windows rendering at 150% DPI, real-device listening and installation
acceptance remain outstanding. Wine GUI tests do not establish those results.

## v0.2.2 follow-up

Input Hub and Output Hub are independently selected above each computer icon.
The selected role is labeled Input Hub/Output Hub; the other computer offers
Use as Input Hub/Use as Output Hub. Input and Output device dropdowns are side
by side beneath the icon. Selecting the current Windows hub is a no-op, avoiding
an unnecessary role command and protective mute. The native Windows fixture
checks role placement, dropdown alignment, independent role changes and labels.

## v0.2.3 compact controls

Input and Output fields retain equal widths when a device name is long. The Mac
uses bounded native pop-up buttons with tail truncation; full names remain in the
menu and tooltip. Windows retains its fixed-width native dropdowns.

Each computer now has adjacent microphone and headphones icon buttons for its
existing mute and deafen controls. Active controls show red slashed icons; native
button states, accessible action names and tooltips expose their meaning. Input
Hub and Output Hub controls remain above the computer icons.

Validation: the Mac production build and model/IPC tests passed; its fixture UI
was visually checked with long names and both icon states. The strict Windows
build and native GUI fixture tests under Wine passed, including all four button
actions, acknowledged states, changed icons, and stable long-name field bounds.
This release changes presentation only; engine, room agent and drivers are unchanged.

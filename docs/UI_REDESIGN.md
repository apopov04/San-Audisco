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

## v0.2.4 hub and global controls

Two compact crowned icons above each computer select its Input Hub (microphone)
and Output Hub (headphones) roles. Selected roles are blue; hover descriptions
explain each role. The bottom section is labeled Global audio and uses the same
microphone/headphones controls as the device sections.

Device icons show effective control state: global deafen marks both headphones
and microphones as blocked, global mute marks both microphones, and a device's
deafen also marks its own microphone. An inherited block disables its subordinate
button with an explanation of which control to release. Individual preferences
are preserved when global gates are removed. This changes UI state projection,
not engine settings or the meaning of mute/deafen.

The Windows GUI fixture suite verifies crowned square hub buttons, global gate
propagation, unchanged individual preferences, restoration, and the existing
room/route behavior. Mac visual checks exercise the same global propagation and
independent hub selections; its model/IPC test and both production builds pass.
Windows GUI validation uses Wine, not the physical gaming PC.

## v0.2.5 Golden Gate Audio

The display name is now Golden Gate Audio. A vector suspension bridge replaces
the central link: international-orange red when the room is connected, gray
otherwise. The existing connection-status text remains below it; stopping audio
does not disconnect the room or gray out the bridge. Both native UIs draw the
bridge without bitmap assets or additional runtime dependencies.

The global slider is labeled Master volume: it scales both playback sources
together, leaving microphone levels unchanged. Start/Stop audio now has hover
help explaining that it controls playback and microphone relay on both connected
computers while preserving the room pairing.

Existing AudioBridge executable names, installation paths, bundle identifiers and
room data remain compatible. The installer updates the existing application; a
new pairing or duplicate background service is not required for this display rename.
Mac visual checks cover connected, stopped-but-connected and disconnected states.
The Mac model suite and Windows native GUI fixture suite under Wine pass.

## v0.2.6 San Audisco

San Audisco is the chosen display name across the Mac and Windows app, menus,
permission guidance and installer. The Golden Gate bridge graphic and its
connection states remain unchanged. Existing AudioBridge installation paths,
executables, bundle identifiers and pairing data are retained for compatibility.
This release changes naming only. Both native apps and installers build; the
Mac signature/disk image and Windows installer payload are verified.

## v0.2.7 bridge traffic

Short horizontal dashes travel in both directions along the bridge roadway while
the room is connected. They disappear when disconnected; the bridge remains gray
and retains its status label. This is connection activity, not an audio meter.

The native animation targets 30 frames per second and respects the OS animation
preference (static dashes with reduced motion). Mac redraws only the small Canvas
inside the bridge and pauses its timeline when the scene is inactive. Windows
uses a separate presentation timer, invalidates only the roadway, buffers the
small graphic, and stops its timer when disconnected, hidden, minimized or on
another tab. No extra agent polling or audio processing is scheduled by animation.

Validation: native Mac screenshot sequences confirmed opposite travel directions
and a stationary disconnected bridge. The actual Windows renderer test under Wine
confirmed moving connected frames, no disconnected dashes, OS animation preference,
and timer suspension. The Win32 GUI regression suite passed, including a check
that repeated animation ticks do not trigger IPC polling. Both production builds
passed; physical Windows performance/listening was not retested for this UI change.

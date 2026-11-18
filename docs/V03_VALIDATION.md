# San Audisco 0.3.0 validation

Date: 2026-11-18. Builds: macOS arm64 (minimum macOS 13) and Windows x64.
This is a private test release. The existing 0.2.10 installations were left
intact during this verification.

## Automated core and routing

- Native CMake suite: 26/26 passed (81.80 seconds). Includes engine signal
  delivery/suppression, authenticated room transport, dependency policy,
  device classification, Windows–Mac, Windows–Windows and Mac–Mac room cases.
- Final targeted handover, migration and malformed-protocol regression checks
  passed after the last agent changes.
- All four Input/Output Hub combinations are exercised using two real agent
  processes and the production engine with synthetic audio endpoints.
- Handover tests interrupt each of five transfer stages, restart the affected
  agent, and verify paused recovery and an explicit muted restart.
- Migration tests cover reversed IDs, differing old caches, preserved pairing
  and custom names, credential denial without rewriting saved data, and stale
  coordinator recovery. Old application protocols cannot start audio.
- Device-profile tests include malformed firmware and randomized input.
  The local Mac reports MacBook Air/laptop (Mac17,4); the actual Windows PC
  reports desktop (SMBIOS chassis 3).

## Native interfaces

- Mac Swift model checks and a macOS 13-targeted build passed. The branded
  Audio checks preview was inspected. A final installer preview was blocked
  by the locked desktop; an earlier preview of the same setup source was
  inspected.
- Windows main/settings/checks UI tests passed at 96, 120, 144 and 192 DPI
  (100%, 125%, 150%, 200%) under Wine and on the actual Windows PC.
- Checks include non-overlapping header actions, separate bridge/status
  rectangles, constrained long names, small-window scrolling, ID-addressed
  controls, global mute/deafen state, room-code validation and normalization,
  same-platform participants, and reconnect handling.
- An initial native Join test failure was a harness defect: SetWindowTextW
  cannot populate a foreign process's edit control. The corrected harness uses
  WM_SETTEXT and verifies text delivery before clicking Join. The full native
  suite then passed at all four scales; the original failure logs were retained.
- Native GDI rendering/animation tests passed. Render artifacts are composed
  control renders, not claims of captured physical display pixels.

The remote Windows run also passed all three platform-pair integration cases,
handover, migration and protocol tests with synthetic endpoints. Test executables
used isolated data and window classes; production process IDs stayed unchanged.
Native results are retained on that PC under
`C:\Users\acesp\Documents\ChatGPT\AudioBridge\SanAudisco-v03-NativeTests`.

## Packaging

- Final installer payloads match the final production GUI, agent and engine
  hashes. Test fixtures are excluded from production packages.
- Windows installer payload check passed; eight package/safe-UI tests passed.
- Seven native package guards passed. Mac manifest checks and strict deep
  code-signature verification passed. Mac signing remains ad hoc, not Developer
  ID notarization; OS permission prompts are still possible.
- Both apps/installers report version 0.3.0; Mac build number is 13.
- Vendor driver packages remain unchanged; setup detects existing dependencies.
  No drivers were installed and no reboot was performed during these tests.

## Limits and remaining live acceptance

Synthetic endpoint tests verify routing and state changes, not physical Bluetooth
latency, dropout rates or Deadlock performance. Same-platform rooms have not been
tested with two physical Macs or two physical Windows computers in this run.

The final live Mac–Windows check is pending: the Mac was locked and the selected
AirPods endpoint was unavailable on Windows. Before calling the installed update
hardware-validated, unlock the Mac and connect the AirPods, then verify:

1. Existing room and endpoint selections survive updating both apps.
2. Mac and PC playback are audible together, with no unintended speaker output.
3. The selected microphone reaches both application-facing inputs, and mute and
   deafen on either UI suppress the intended routes.
4. Output/Input Hub swaps settle on one agreed state without duplicate playback.
5. Stop/quit restores the previous OS default devices; reconnect does not select
   a fallback speaker automatically.

A short synthetic benchmark measured about 1.9% and 2.3% of one CPU core for the
two engine processes on this Mac, with about 6.8 MiB peak RSS each. This excludes
native audio drivers, the GUIs, Bluetooth and the game; it is not a PC performance
or end-to-end latency measurement.

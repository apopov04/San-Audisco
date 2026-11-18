# San Audisco v0.3 architecture

## Processes and ownership

Each computer runs a native UI, one per-user `bridge-agent`, and an owned audio
engine while audio is enabled. The UI uses authenticated local loopback IPC;
closing the window leaves the agent and engine alive. Explicit Quit stops only
the local agent and its owned engine. UI/login startup runs `ensure`, which uses
a private instance lock rather than starting duplicate agents.

The agent handles discovery, pairing, device inventory, setup, persistence,
remote commands, and engine supervision. It does not move audio samples. Audio
continues over the existing encrypted UDP/Opus engine transport. The UI is SwiftUI
on macOS and native Win32 on Windows; there is no embedded browser runtime.

The selected Output Hub agent coordinates shared settings independently of OS.
Both UIs address stable computer IDs; platform selects only the native backend.
Epoch/revision-tagged snapshots reach both child engines over private IPC.
The media channel acknowledges state but cannot change it. Handover closes the
old output before committing the next generation; ambiguous transitions pause
until both devices reconcile and the user explicitly starts again. See
[V03_ARCHITECTURE.md](V03_ARCHITECTURE.md) for migration and state transitions.


## Audio routes

Each device has four possible endpoints:

1. Playback capture from its first virtual cable.
2. Microphone delivery into its second, independent virtual cable.
3. Physical headphone output, opened only by the selected Hub.
4. Physical microphone input, opened only by the selected microphone-source device.

Local playback reaches the Hub's listening mixer directly; satellite playback
crosses the network and joins that mixer. The selected physical microphone reaches
both cable microphone outputs, subject to the destination/global gates. Diagnostic
tones enter playback only. San Audisco does not open the AirPods microphone when
the MacBook microphone is selected, preserving the stereo Bluetooth playback path.

Codec frames, listening queues, clock correction and conservative Wi-Fi reserves
are unchanged from the working baseline. Meters calculate bounded RMS statistics
in the worker; no network/control/UI work is added to an audio callback. An
explicit test tone uses bounded preallocated scratch storage in playback capture.
When no test tone is active the original capture path is used directly.

## Room and credential lifecycle

The room module provides UDP discovery plus authenticated TCP control. An eight
character expiring SPAKE2 invitation creates a saved master credential. Discovery
contains no invitation or key. Saved credentials authenticate reconnection and
address changes. [ROOM_PROTOCOL.md](ROOM_PROTOCOL.md) specifies the limits and
cryptographic assumptions; this new integration has not had an independent audit.

The agent stores only the master credential in Keychain/DPAPI. An independent media
key is derived and sent over the private inherited pipe to its child engine. Keys
do not appear in arguments, configuration, logs or discovery. Private state and IPC
token files use owner-only permissions/ACLs. IPC binds loopback and rejects callers
without the per-user random token; it is not an administrative service.

State is atomically saved in `AudioBridge/room-v2`. It contains identity, non-secret
peer metadata, device selections, acknowledged mixer settings and running intent.
Old application configuration remains untouched. A legacy route-only backup is
created for migration; old manual pairing keys are never reused for room security.

## Recovery and safety behavior

A lost room connection stops the local engine and preserves running intent.
Authenticated reconnection retries audio startup, using protective global mute.
Explicit Stop clears running intent. A pending reconnect can also be stopped from
the UI. An engine exit, missing endpoint, or permission failure produces a visible
status and bounded retry. There is no automatic Bluetooth pairing or permission
approval, and the app never kills unrelated audio/game processes.

The engine continues to handle callback stalls, device changes and network
re-synchronization using its existing recovery logic. Protective mute is explained
in the UI, including cases where effective muting comes from missing readiness.
A user must explicitly unmute after a reconnect or role change.

## Setup and release boundaries

Private platform setup wrappers embed the exact original third-party components,
verify them, install only missing components, and re-inspect actual endpoints.
The app itself installs per user; only vendor driver installers request elevation.
Setup never reboots or silently replaces a running audio app. Hi-Fi Cable format
configuration remains a guided OS step. Permission approval and application device
choices remain local to the OS/Discord/game.

Source-only archives and private installer payloads are separate. Mac app/setup
use ad-hoc signatures, so production signing/notarization remains outstanding.
Native Windows drivers, Bluetooth, actual reboot flows and gaming performance
cannot be validated with synthetic audio or Wine; see V02_VALIDATION.md.

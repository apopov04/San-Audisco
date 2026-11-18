# San Audisco 0.3 — device identities and Output Hub coordination

## Scope and invariants

A room has exactly two authenticated computer identities. Either participant can
run Windows or macOS. OS selects the native backend and dependency adapter; it
never determines the participant, icon, microphone owner, mixer owner or settings
authority. The two IDs sort into persistent deterministic slots A/B. UI commands
use IDs; slots exist only for bounded engine arrays and serialized state.

The Output Hub agent is the sole shared-settings authority. Both UIs issue
ID-addressed commands to their local agent. A follower forwards shared changes to
the Output Hub over the authenticated reliable room connection. The Input Hub
captures the selected microphone and supplies both virtual microphone sinks.
The Output Hub's existing listening mixer combines local and remote playback.
Bluetooth pairing itself remains controlled by the OS.

## Shared state and engine boundary

Room snapshots advertise application protocol 3 and a shared document containing
schema, authority epoch, settings revision, the ordered pair of participant IDs,
and complete settings. Old peers can retain their saved pairing but audio startup
is blocked with an update message until both advertise protocol 3.

The native child engine takes `slot=0|1` rather than `device=windows|mac`.
Its stdin receives `state EPOCH REVISION SNAPSHOT`, with settings rows separated
by `|`. Engine media control packets are only `V3 EPOCH REVISION SLOT` applied-state
acknowledgements; they cannot change settings. Authenticated media sessions have
fresh transport epochs, so a replaced engine cannot replay an old audio session.
Older authority/revision state frames are rejected. Reopening microphone delivery
requires acknowledgement of the enabling revision; ordinary volume changes do
not interrupt voice. Audio callbacks never perform networking, persistence or
control-plane waits.

## Handover

Output and Input Hub changes use prepare / prepared / commit / committed /
finalize messages with a transaction ID and the next complete shared document.
Both engines stop before commitment. The next epoch starts muted. Each stage is
journaled in the existing private state directory. The old listening output is
closed before the new Output Hub may resume. Input changes retain the same
settings coordinator. No automatic speaker promotion occurs on disconnect.

On ambiguous failure, both sides stop and retain the last committed document.
After reconnect they reconcile to the committed generation and require explicit
Start. Recovery flags carry the epoch/revision that caused recovery, so stale
recovery messages cannot undo a later coordinated resume. Startup is coordinated
by the Output Hub, advances the revision and begins muted.

## Upgrade

The v2 state is backed up before conversion. Legacy Windows/Mac gains, mute flags,
balance direction and selected roles map to IDs/slots once. A persisted migration
flag gates controls until the legacy Windows authoritative cache seeds the shared
v3 document; subsequent authority belongs to the selected Output Hub. A failed
credential read aborts startup without overwriting state or forgetting the room.
Existing credential service names, room secrets, app identity and install paths
remain unchanged. New state must not be opened by an old executable without
restoring the backed-up old schema.

## Device presentation

Each endpoint publishes kind (`laptop`, `desktop`, `server`, `unknown`), model,
default name and manual kind override. Detection is local and cached. Windows
uses bounded SMBIOS chassis parsing. Mac uses known hardware model mappings and
an internal-battery fallback for otherwise unknown notebook models. Unknown
models use conservative defaults. No serial number is collected. Custom names
are preserved; icon overrides are optional. Mac Studio uses the server icon by
product choice, not because the hardware is necessarily running a server OS.

Both native UIs render stable participant slots with real names and use the Mac
layout: header actions, two device cards, bridge/status, per-device routes and
controls, Global audio footer. Windows settings/checks use native owned windows;
Quit remains in system/tray menus. Bridge painting and connection text have
separate rectangles. Installers retain original vendor packages and normal OS
consent while presenting a branded guided check/install/ready flow.

## Verification boundaries

`agent_integration_test.py` runs real agents and the production engine against
synthetic endpoints for all three OS pairs and four role assignments.
`agent_handover_test.py` interrupts each handover stage and verifies paused
recovery, single output ownership and explicit muted resume. Runtime audio tests
measure generated signal delivery and suppression rather than state labels alone.
Native UI model/GDI tests and Wine compatibility tests are separate from physical
Bluetooth/audio acceptance. Release evidence must distinguish them explicitly.

# 0.3.2 — resize and Quit restoration

The Quit-blocking behavior described here was corrected in
[0.3.3](V033_QUIT.md): restoration warnings no longer prevent exit.

## Changes

Windows artwork now scales uniformly with DPI. Resizing moves the device and
bridge anchors horizontally without stretching their shapes. Layout changes
invalidate the parent and all child controls and disable Windows' copying of
old child-window pixels. This removes the maximized-view duplicate artwork and
ghosted labels while retaining the static artwork cache for animation.

Quit now sends Stop to the paired computer and waits up to two seconds for its
acknowledgement before closing the local agent. Both sides clear their saved
start intent; reopening preserves pairing and stays stopped until Start. The
other app remains open. An unavailable peer restores locally after its ordinary
disconnect detection. Close-window/X still keeps audio running, as documented.

Normal routing restoration first uses each role's write-ahead record. For older
or manually selected cable defaults with an empty record, it can recover to the
explicitly selected, available physical output/microphone. It never replaces an
existing restore record or changes a different manually selected output. No
arbitrary speaker is chosen when the physical selection is unavailable. Quit
reports restoration failures instead of silently treating them as success; the
app stays open so the device can be reconnected and Quit retried.

Recovery also runs for an idempotent Quit when the agent has already exited,
without loading room credentials or starting capture. While an agent remains
open but audio is stopped/unhealthy, it retries restoration periodically.

## Evidence

- Reproduced on the actual Windows PC: maximizing duplicated bridge/laptop
  artwork and ghosted labels; restoring the size cleared them.
- Both machines had virtual system defaults and empty restore journals. On the
  Mac, the new recovery code restored `BuiltInSpeakerDevice` and
  `BuiltInMicrophoneDevice`; CoreAudio readback confirmed both, with alert output
  unchanged. No audio capture or test tone was needed.
- Routing unit tests passed, including legacy cable adoption, preservation of
  manual choices, original-record priority, unavailable fallback, and existing
  crash/unplug/retry/write-ahead checks.
- Real-agent synthetic integration passed for Windows–Mac, Windows–Windows and
  Mac–Mac: acknowledged room-wide Quit, both engines stopped, both input/output
  journals cleared, pairing survives reopen, and audio stays stopped until Start.
- Dedicated real request-path tests passed for both platform inventories with
  no running daemon: recovery, idempotence, manual selection preservation and
  explicit failure when the physical output is unavailable.
- Agent handover/crash, migration and malformed-protocol regression suites passed.
- Renderer regression checks compare normal/wide/restored device pixel bounds
  and counts, cached versus direct pixels, cache invalidation, and motion state.

Physical AirPods listening acceptance is deferred at the user's request. The
Windows AirPods endpoint was unavailable during investigation; its saved
selection is preserved. OS restoration cannot make a disconnected endpoint
play audio. Explicit per-app output selections still override system defaults.

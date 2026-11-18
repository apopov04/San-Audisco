# 0.3.3 — Quit without requiring an available output

The 0.3.2 Quit guard conflated two outcomes: successfully stopping audio and
successfully restoring every OS audio default. A stopped Windows peer with an
unavailable AirPods output returned a restoration error; the Mac then refused
Command-Q, including after reopening without starting a new audio session.

Quit now stops local audio and clears restart intent, requests peer Stop and
waits for its acknowledgement, then exits regardless of output availability.
Restoration warnings are returned separately as `state.quit_warning` and saved
as `last_quit_warning`. Existing restoration journals, pairing and selected
routes are retained. The peer's own UI continues showing its current restoration
warning and retrying while open. A peer acknowledgement error from 0.3.2 is also
nonfatal, so the Mac fix works while updating the other computer separately.

A new peer acknowledges that Stop succeeded with `ok: true` and includes a
separate `warning` when restoration is pending. A missing acknowledgement does
not trap Quit: the local engine stops and exits, and the peer's existing
disconnect handling stops its engine. No arbitrary replacement speakers are
selected. Reopening preserves the room and leaves audio stopped.

Idempotent Quit with no running daemon follows the same rule. A disconnected
physical output returns a warning with success, preserving recovery information
instead of returning a failure that prevents the GUI from closing.

## Validation

- A new isolated two-agent test reproduced the exact reported Mac error on the
  previous build before the change.
- The corrected test pairs a Mac with a Windows peer whose default output is its
  cable but whose selected physical output is unavailable. It quits the Mac,
  reopens the saved room without starting audio, and quits again. Both attempts
  succeed; the peer remains open and stopped; pairing and routes survive.
- The same test checks Quit with a missing local output. An optional old-peer
  executable exercises compatibility with the earlier error response.
- The no-daemon recovery test covers missing-output success with a separate
  warning, idempotent Quit, manual-output preservation and saved stopped intent.
- Routing lease and real-agent audio integration regressions pass. These tests
  use isolated agents, synthetic endpoints and test-only OS backends.

Closing a window still keeps audio running in the background. Explicit Quit
stops it. Device restoration cannot make disconnected headphones available;
that condition is now separate from whether the application can exit.

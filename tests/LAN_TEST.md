# Optional two-host LAN test

Run this test explicitly; it is not registered with default CTest. Use current
`runtime_fake` builds on both computers. This executable uses the production CLI,
codec, encrypted transport, control and mixer with generated audio callbacks.
It does not open physical microphones or headphones and does not measure native
driver performance, Bluetooth latency, Discord or game performance.

Build the `runtime_fake` target for each host and provide Python 3. The script
imports the adjacent `runtime_test.py`; keep both files together. Stop other
San Audisco network tests. Both peers use UDP port 45672 by default and need
each other's numeric IPv4 address. A different port must be specified on both.

Generate one **disposable** pairing-key file using `runtime_fake keygen` or the
San Audisco CLI's `keygen` command, then transfer the same file privately to the
other computer. Pass its explicit local path with `--key` on each host. Do not
use a real session's key, commit a key, or store keys/results in `tests/`. Delete
the disposable copies when the test is finished.

Start the Mac observer first, from the repository root:

```sh
python3 tests/lan_diagnostic.py mac /absolute/path/to/runtime_fake \
  --peer WINDOWS_IPV4 --key /absolute/private/path/test.key \
  --output /absolute/path/to/new-mac-results
```

Then start the Windows controller in PowerShell, from its repository root:

```powershell
python tests\lan_diagnostic.py windows C:\path\to\runtime_fake.exe --peer MAC_IPV4 --key C:\private\test.key --output C:\path\to\new-windows-results
```

Use the actual Python executable path if Python is not on `PATH`. The Mac waits
up to 600 seconds for pairing (`--wait-seconds` changes that limit). Output
directories must not already exist, so earlier evidence is preserved.

The 16 phases exercise mixing, independent microphone mute and deafen, source
volume, balance, listening-only silence, primary and microphone-source changes,
and global mute/deafen. Each side continuously checks its own output during the
three-second assessment, after collecting fresh metric windows. Windows holds
the phase for at least four seconds and waits for the Mac's acknowledgement.
The Mac sends master gain 0.99 for a passed assessment or 0.98 for a failed one;
Windows resets it to 1 for the next phase. Gain 0.97 marks final completion.
These test markers use the existing control protocol and are sent only after
the Mac's main assessment. A missing acknowledgement, failed audio check or
observed loss of connection fails the phase. The Mac continues checking
connection health until it observes the next phase; it does not grade the old
audio expectation during that transition because state telemetry can lag audio.

Expect roughly 80–100 seconds after pairing, possibly longer while waiting for
acknowledgements. Do not relax thresholds to obtain a pass. The calibrated tone
indices are Windows playback 410 Hz, Mac playback 730 Hz, Windows microphone
1090 Hz and Mac microphone 2390 Hz. Existing three-window median thresholds
remain: wanted tone at least 0.04, absent tone at most 0.008, and silence RMS at
most 0.002. Quarter-volume bounds remain 0.022–0.052.

**Both processes must exit 0 and both `report.json` files must show all 16 phases
passed.** One host passing is insufficient: the Mac can observe a late failure
after sending its initial acknowledgement. Keep both reports, `states.jsonl`
and `audio.jsonl`; the latter contains generated-tone metrics, not recordings.
`peer.conf` contains the key's path, not its contents. Production telemetry is
sampled approximately once per second, so this test does not establish that
every shorter connection event was observed. No diagnostic transport tracing
is required or claimed. Preserve failures for diagnosis before any justified
rerun, using a fresh output directory.

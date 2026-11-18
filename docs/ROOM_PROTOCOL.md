# San Audisco room protocol v2

This protocol manages a two-device (one Windows, one Mac) LAN room. It does not
replace or change the audio codec, audio engine, or real-time transport.

## Implemented dependencies

Password-authenticated key exchange uses the **BoringSSL SPAKE2 implementation**,
pinned to commit `ebd922adc708a48b0cccfcdf81e854a3fdc1b7b6` of
https://github.com/google/boringssl. The source archive SHA-256 is
`75ee588268ae3d7552f5be7e0849bdcc8e4159f22547dbfb4aae997784fdd345`.
`scripts/bootstrap-room-crypto.py` verifies this digest and builds the static
`crypto` target. The license is retained in `third_party/licenses/boringssl.txt`.
No external crypto program or runtime service is installed on either machine.

BoringSSL was selected over the small older CPace examples because its maintained
implementation provides an established SPAKE2 primitive, strong randomness,
constant-time elliptic-curve operations, and explicit role/identity binding. This
is BoringSSL's deployed SPAKE2 variant, **not a claim of RFC 9382 wire compatibility**.
San Audisco's framing, confirmation, discovery and reconnect integration are new
application code; passing these tests is not an independent security audit.

## Invitations and discovery

An invitation contains eight uniformly random characters from a 32-character
alphabet (40 bits), displayed as `XXXX-XXXX`. Parsing ignores ASCII spaces and
hyphens and accepts lowercase. Ambiguous characters are not generated. Invitations
expire after five minutes, are single-use, and are wiped on success, cancellation,
expiry or forgetting the room. They are not persisted.

UDP discovery uses port 45671 by default. A query is answered with protocol ID,
opaque random room ID, device ID/name/platform, TCP port and whether an invitation
is open. **No PIN, password hash, PAKE verifier or key is advertised.** Discovery
is untrusted; no saved address or device identity is changed until authentication
succeeds. A manual-address probe sends the same bounded query as unicast UDP.
This handles blocked broadcast, not networks that block all peer traffic.

One room admits one Windows and one Mac. Already paired rooms do not accept
another invitation. Pairing guesses are limited globally to ten per minute and
twenty per invitation. There are at most eight concurrent handshake connections,
with ten-second deadlines. Discovery retains at most 64 devices for ten seconds.
A local network attacker can disrupt availability; discovery is not a mechanism
for defending against Wi-Fi jamming or network isolation.

## Pairing and control

TCP messages are length-prefixed. Handshake frames are limited to 4096 bytes.
Both parties exchange device identity/name/platform, room ID, service port and
fresh 256-bit nonces. A canonical transcript binds all those values, the protocol
version and the pairing/resume mode. SPAKE2 role names bind this transcript into
the PAKE operation. The short code is supplied only to SPAKE2.

After SPAKE2, a 256-bit master credential is derived with BLAKE2b from the full
SPAKE2 output and transcript. Each party proves possession using a separately
labelled keyed BLAKE2b confirmation. Neither publishes a credential or signals
successful pairing until the peer confirmation verifies.

Authenticated control traffic uses libsodium XChaCha20-Poly1305 with separate
client-to-server and server-to-client keys. Keys are derived from the master
credential and the fresh session transcript. Each direction has a strict 64-bit
sequence, authenticated with the frame type; repeats, skipped sequence numbers,
modified tags, plaintext after authentication, and exhausted counters terminate
the connection. Nonces encode the direction's sequence under its distinct key.
Payloads are at most 64 KiB; queues are bounded to 512 KiB and 128 delivered messages.
Encrypted heartbeats detect a silent peer after five seconds.

The audio transport key is derived separately using the room ID and an explicit
`audio` domain label. The daemon stores the master credential in Keychain/current-user DPAPI. It derives
the audio key in memory and sends it through the owned engine's inherited stdin
pipe; plaintext configuration contains no keys.
BoringSSL and libsodium need not run in the real-time audio callback.

## Reconnection and roles

The lexicographically lower device ID initiates remembered-room reconnection,
which avoids simultaneous competing connections. Both peers authenticate using
the saved master credential and fresh transcript nonces; fresh directional control
keys result. A discovered new address is only committed after mutual confirmation.
The PAKE exchange is not repeated and the invitation code is not needed.

Reconnect sessions do not currently add an ephemeral Diffie-Hellman exchange:
compromise of the saved master credential also compromises recorded control
sessions. Do not claim forward secrecy for remembered sessions.

Room creator/client/server roles are unrelated to Hub/Satellite audio roles.
The daemon, rather than this transport, owns authoritative settings revisions,
remote device validation, audio startup, and configuration persistence. The daemon
must save `take_credential()` successfully before enabling media and must handle
credential-store failures without presenting the device as successfully paired.

## Validation

`room_test` runs actual loopback UDP/TCP instances and covers discovery, PIN
validation, correct/wrong codes, identity agreement, bidirectional encrypted
messages, size bounds, third-member rejection, remembered credentials, wrong
saved keys, expiry, cancellation, manual unicast discovery and rate limiting.

`room_wire_test` places a TCP relay between actual instances and verifies rejection
of modified ciphertext, replayed ciphertext, and altered handshake device names
or service ports. Tests use synthetic identities and disposable credentials only.

# v0.4.0 — first GitHub preview

This release prepares the working two-computer application for repeatable builds
and an app-only download. It does not add new audio-routing features.

## Cleanup

- Removed an unused audio ring implementation, control serializer, state fields,
  redundant microphone inspection buffer, and tests that only exercised the
  removed implementation. The production adaptive ring and microphone send path
  remain in use.
- Removed hidden legacy Windows hub selectors and their handlers, unused window
  bookkeeping, and unused Mac model state. Updated visible role terminology,
  connection text, command-line help, and menu-bar labeling.
- Kept legacy AudioBridge bundle, credential, application-data, migration and
  protocol identities deliberately. Renaming these would reset permissions or
  pairing, lose access to saved configuration, or break compatibility. They are
  not obsolete display names.
- Centralized package version/build metadata, refreshed setup documentation, and
  marked old validation reports as historical.
- Fixed an MSVC Windows macro collision in the transport build and made the
  installer-policy checks run in optimized Release builds.

## Release boundaries

Release packaging requires a clean committed source tree, verifies Mac bundle
metadata, and writes source-commit provenance and SHA-256 checksums. App-only
archives contain production binaries, documentation and library notices; private
driver installers and test programs are excluded. Existing release files cannot
be silently overwritten.

The private full setup packages remain a separate personal installation path.
See [distribution and installation](DISTRIBUTION.md) for driver prerequisites,
vendor terms and the difference between these packages and the GitHub ZIPs.

## Verification

On the development Apple Silicon Mac, the optimized native C++ build passed all
28 existing CTest suites. The added Swift room-model check also passed. The
production SwiftUI app compiled and its ad-hoc signature was verified. Windows
x64 production binaries cross-compiled; the native UI and render-test sources
also compiled with warnings treated as errors.

The engine, control parser, room pairing and adaptive-ring suites also passed
with AddressSanitizer and UndefinedBehaviorSanitizer enabled. Nine package tests
cover provenance, missing inputs, version mismatches, overwrite prevention and
exclusion of unexpected Mac bundle payloads.

CI runs the native Windows suite, including four GUI scale settings, and the Mac
suite including release-boundary tests. The GitHub release notes link the results
for the published commit. CI uses fake audio where hardware is unavailable;
passing those tests does not measure Bluetooth latency or game performance.

No new live AirPods/Deadlock session, clean-machine vendor-driver installation,
older-macOS execution, signing/notarization, or store submission is claimed for
this cleanup. The user's installed v0.3.3 apps were not replaced during release
preparation. Earlier successful listening reports apply to that preceding build.

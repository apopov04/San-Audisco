# 0.2.9: automatic playback routing and Windows redraws

> Historical version record. Statements about pending tests, installed versions,
> and temporary artifact paths describe the run recorded here. For current behavior
> and installation, start with [README](../README.md). Temporary build/test folders
> were removed during the November 18, 2026 cleanup; retained evidence is in
> `validation/` and the owner’s cleanup archives.


## Routing behavior

The room agent owns a per-computer output lease. It waits for the local engine
to be synchronized, both playback captures to be ready, and the selected Output
Hub's headphones to be ready before switching system playback into the cable.
The audio engine continues to open the explicitly selected headphone endpoint,
so it never plays its mixed output back into the capture cable.

- Mac: change the default media output to the same duplex BlackHole device used
  by playback capture. Alert/system-effect output is left at its prior selection.
- Windows: resolve the VB-Audio capture endpoint to its matching render endpoint
  by the known driver names. For the standard setup this is CABLE Output →
  CABLE Input. Do not choose CABLE In 16ch or the separate microphone cable.
  Save and switch Console, Multimedia and Communications render defaults
  separately. Recording defaults are also managed after choosing Autoconfigure: the shared
  microphone cable becomes the system input while audio is ready.
- Stop, Quit, Forget or room disconnect restores only defaults still pointing
  at the cable selected by the app. A user/OS override is not overwritten.
- If the media connection or headphone readiness fails while the room remains
  connected, restore after three seconds of unhealthy readiness (checked once
  per second). A healthy connection can acquire the route again.
- An atomic, private `room-v2/output-restore.json` journal is written before OS
  changes. Partial failures roll back. The next launch repairs a lease left by
  a crashed agent. A force-killed agent cannot restore until relaunched.
- Missing original hardware retains the recovery record for retry. Errors are
  visible in Audio checks; manual system output selection remains available.

The app does not change per-application endpoint overrides or move Bluetooth
pairing. Autoconfigure enables microphone default management on both peers;
without that explicit action the microphone default remains unchanged. Input
defaults have a separate `input-restore.json` recovery journal and restore on Stop,
Quit, disconnection or loss of microphone readiness. Some applications require restarting
playback to adopt a new system default. Global Deafen does not restore the
speakers: that would make deafened audio audible again.

When upgrading from a version where the user already manually selected the
virtual cable, there is no trustworthy previous physical output to recover.
That existing cable selection is preserved. Before the first automatic session,
select the preferred normal output once while audio is stopped if restoration
to physical speakers/headphones is desired. Subsequent sessions save it normally.

Windows uses the isolated IPolicyConfig default-endpoint setter. This is not a
documented public Windows API; unsupported/failing calls report an error instead
of silently claiming the system output changed.

## Autoconfigure

Both main windows have an Autoconfigure button. It configures both connected
peers through the existing authenticated room control channel. It selects the
known independent virtual cables, preserves available physical selections, and
fills missing physical selections using current physical output defaults, Mac
built-in devices, or unambiguous available choices. It does not select Bluetooth
hands-free endpoints automatically. Ambiguous required choices produce a prompt
to select the desired input/output. It does not install drivers or edit another
application's private settings. Apps following system defaults receive the mix
and shared microphone once audio is started and ready.

The bridge connection label reads **Connected** on both platforms.

## Windows interface

Native labels, button icons/checks, selector selections, progress bars, slider
positions and enabled states are updated only when their values change. An
inventory revision without a real inventory change no longer rebuilds selectors.
Status polling no longer invalidates the whole window. The buffered bridge has
an independent animation timer and redraws its static contents only when device
availability changes. The top-level window clips child controls during painting.

Device silhouettes remain platform based: desktop for Windows, laptop for Mac.
This release does not infer chassis type or change the selected icons.

## Permissions and validation

The build supports persistent Developer ID signing with hardened runtime and
audio entitlements. No signing identity is configured on this build machine;
the produced private artifacts remain ad-hoc signed and **not notarized**.
See [SIGNING.md](SIGNING.md). OS consent prompts are not bypassed or suppressed.

Validation includes production agent integration with synthetic audio and
isolated output defaults; takeover/restore/journal policy fault tests; Mac
production model tests; and real Win32 control/renderer tests under Wine.
Wine validation is not native Windows flicker acceptance. Actual OS switching,
restoration and visual acceptance with the paired hardware require installing
this candidate after the current listening session. No live app was replaced
while the user tested 0.2.8.

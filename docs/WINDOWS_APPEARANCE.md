# Windows appearance — 0.3.1

> Historical version record. Statements about pending tests, installed versions,
> and temporary artifact paths describe the run recorded here. For current behavior
> and installation, start with [README](../README.md). Temporary build/test folders
> were removed during the November 18, 2026 cleanup; retained evidence is in
> `validation/` and the owner’s cleanup archives.


The Windows overview follows the Mac layout: a separated title/action row,
two device columns, crowned Input/Output Hub selectors, the Golden Gate graphic,
side-by-side Input/Output selectors, device volume and mute/deafen controls,
and a separate Global audio section. All three audio-button pairs use the same
44-DIP dimensions and 8-DIP gap. Long device names stay inside their selectors.
Room settings, audio checks and advanced routes remain secondary windows.

## Implementation and limits

- Existing Win32 HWND controls retain their IDs, keyboard handling, notifications,
  tooltips and accessibility objects. A subclass draws their presentation using
  Windows-provided GDI/GDI+, UxTheme and DWM. No web view, browser runtime or GUI
  framework is bundled. Typography uses Windows Segoe UI, not Apple's fonts.
- Room settings has a local System/Light/Dark choice. System follows Windows'
  application color preference. Changes do not send room/audio commands.
- High contrast falls back to native control painting and system colors. Text
  input retains its native caret, selection and editing behavior. Focus remains
  visible for keyboard navigation; checked and disabled states remain native.
- The title bar, taskbar, OS permission prompts and native dialogs stay Windows
  components. This is a matching layout and visual language, not Apple's UI
  framework running on Windows.
- The interface scales with DPI and scrolls at constrained window sizes. Windows
  currently scrolls the entire overview; the Mac keeps its global area separate.
- The static bridge/device artwork is cached by dimensions, connection/device
  state and palette. Animation draws only the six colored dashes. Cache buffers
  are reused and released on teardown. Motion follows Windows' client animation
  preference and stops when hidden, minimized or disconnected.
- The audio engine, agent, protocol, buffer sizes, drivers and saved pairing format
  are unchanged. The corresponding Mac UI includes the header divider and matching
  Global audio button spacing.

## Verification

The production GUI builds with LLVM-MinGW at `-O2 -Wall -Wextra -Werror`.
The Mac build targets macOS 13. Native tests use separate window classes and a
fixture agent, never installed as the production helper.

Windows 10 UI checks passed at 100%, 125%, 150% and 200% in dark appearance,
and 100% and 200% in light appearance. The same six combinations passed under
Wine. Checks exercise selectors, native Space-key toggling, local appearance,
room creation/join, per-device and global controls, role selection, diagnostics,
reconnect, close/quit behavior, clipping bounds, long names, equal audio-button
geometry and scrolling. Native rendered main/settings/checks panels were reviewed.
These images are composed native control renders, not physical-screen captures.

The render test checks unchanged telemetry does not repaint controls, connected
traffic, reduced-motion behavior, timer suspension, device-kind artwork, cached
versus direct static pixels, cache reuse and state/theme invalidation.
The appearance test checks native accessible names and checked/disabled states,
paint-resource stability across repeated light/dark batches, and the high-contrast
fallback. Wine lacks complete native accessibility state reporting; those state
assertions are strict on Windows.

The initial candidate redrew every vector on animation ticks. The native GUI-only
benchmark detected a regression and prompted the static artwork cache. The first
GDI test also measured before first-paint lazy initialization. On native Windows,
the revised test recorded 7 cold handles, 11 after warmup, then 12–13 handles
through all 16 batches and four theme changes (16,000 paint calls). Native
accessible names, checked/disabled states and high-contrast fallback passed.
The final cached build also passed the full UI suites in dark at 100% and light
at 200% on Windows. Native renders remained readable and aligned.

Sequential 20-second samples on the Windows PC (GUI process only; fixture helper
and engine CPU excluded):

| State | Previous GUI CPU, % of one core | New GUI CPU, % of one core | Previous/new RSS |
|---|---:|---:|---:|
| Visible, connected animation | 5.70% | 3.20% | 13.93 / 14.72 MiB |
| Minimized | 1.02% | 1.33% | 14.05 / 14.77 MiB |

The new GUI retained 121 GDI handles in both states. These are short regression
samples, not statistical benchmarks. The minimized 0.31 percentage-point increase
was measured; animation is stopped there. The comparison supports a lower visible
rendering cost with less than 1 MiB extra working set in this run.

Native results are retained under
`C:\Users\acesp\Documents\ChatGPT\AudioBridge\SanAudisco-v031-UI-Tests\results`.
The executable remains a small native binary with only Windows system DLL imports.

No new physical AirPods latency, listening quality or Deadlock performance claim
is made for this presentation-only release. Core validation remains documented in
[V03_VALIDATION.md](V03_VALIDATION.md).

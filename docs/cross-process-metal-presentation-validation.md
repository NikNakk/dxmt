# Validation record — 2026-10-09

The DXMT-managed backend was built and exercised on macOS using a separately
staged CrossOver FOSS 26.3 / Wine 11.0 runtime with `WINEMSYNC=1`. The primary
SteamVR/OpenXR installation was not replaced. Core changes are commit `c394be2`; the regression suite is commit `534737c`.
The fixture and commands are in
[tests/remote-presentation](../tests/remote-presentation/README.md); small raw
measurement files are retained in
[results/2026-10-09](../tests/remote-presentation/results/2026-10-09).

## Confirmed defects and changes

Source inspection confirmed unconditional withdrawal could delete a successor's
property; HWND PID alone could select a renderer-owned child whose Cocoa root
belongs elsewhere; host geometry followed the entire content view; all hosts had
one high stacking value; constructor failure could abort; and initialization
started a background host directly during DLL attach. These are code defects,
not claims that every corresponding Steam failure was reproduced here.

The new protocol serializes publication, identity checks, acknowledgements and
withdrawal with a per-window mutex. Replacement generation assertions passed
during the fixture. Actual root ownership is used for discovery, while attachment
and placement keep the child HWND. The renderer-owned child fixture passed with
child PID equal to renderer PID and root PID different. Missing private APIs
returned `DXGI_ERROR_NOT_CURRENTLY_AVAILABLE` without an abort. Geometry,
visibility, clipping and ordering are updated independently of buffer resizing.
Retirement holds the outgoing native context, rather than relying on the host
reference, with per-context records and bounded handoff rules.

## Build and executable checks

* Modified x86_64 D3D11, DXGI, WineMetal PE and native modules built successfully.
  An existing `dxgi_adapter.cpp` missing-return warning remains.
* Native production geometry/protocol tests passed under Clang with
  `-Wall -Wextra -Werror`. Meson's `remote-presentation-rules` passed, 1/1.
* i686 compilation of the rules passed, including fixed ABI size assertions.
  A complete PE32 build was blocked by unavailable Wine32 `winecrt0`; no 32-bit
  runtime result is claimed.
* The Windows fixture and both Swift measurement tools compiled successfully.

| Fixture | Result | What it establishes |
| --- | --- | --- |
| Same-process, `host` configured | Exit 0 | Ordinary local creation/presentation API path |
| Cross-process root | Exit 0 | Root host acquisition and presentation submissions |
| Renderer-owned child / foreign root | Exit 0 | Ownership mismatch handled without missing-data dereference |
| Siblings with churn | Exit 0 | Replacement properties survive retiring predecessor; hide/zero/restore and ordering transitions execute |
| Child, `offscreen` | Exit 0 | Diagnostic API fallback remains functional; no visible-output claim |
| Child, private APIs disabled | Expected exit 1, HRESULT `0x887a0022` | Graceful failure instead of constructor abort |
| Host termination during rendering | Exit 0 | Source cleanup completes after host disappearance |

In cross-process modes the host imports DXGI and does not load D3D11; the renderer
loads D3D11 dynamically. Logs confirm the new v2 protocol, not an accidentally
selected installed DLL. Initial attempts using WINEDLLPATH selected older modules
and were excluded from these results. The staging helper copies the Unix loader
and modules and records module hashes to prevent that mistake.

## Actual compositor samples

Five repeated sibling churn launches produced 200 screenshot samples with zero
raw near-black central patches; a static control produced 40 with zero. Sampling
intervals across those churn trials ranged approximately 185–297 ms, median
248 ms. These earlier captures predate the alpha/geometry validity fields and
must not be retroactively described as fully validated resize samples.

Logs recorded at most two current hosts and up to four pending retired source
contexts in these trials. Each renderer completed 18–21 replacement iterations
without API/generation assertion failures. This is a small fixture, not a leak
proof or a measurement of all contexts in Steam. Host attachment acknowledgement
is not a compositor-visible frame fence.

A separate Retina-mode trial reached actual root client sizes **2401×1500** and
**2400×1501**, plus 2399×1499 and 1000×700. It completed 632 submitted fixture
frames and 21 replacement iterations without assertion failures. Among 40
screenshots, one raw central patch was near-black. Inspection showed an intact
smaller green window, including its title bar, inside transparent padding from a
resize between capture metadata and screenshot. Central alpha coverage was
0.0353. This sample is inconclusive for presentation, not counted as a clean
frame or a demonstrated blank host.

Right/bottom probes found no white fixture-background pixels. Coverage checks
invalidate two right-edge probes and one bottom-edge probe; the remaining
38 right and 39 bottom probes were opaque. This limited screenshot result does
not establish absence of every one-device-pixel seam during live resize.
A second Retina churn trial using the revised capture tool returned exit 0 for
both fixture and capture. Of 40 samples, 28 had opaque central patches and
unchanged before/after geometry, with zero near-black patches; 12 were
inconclusive. The edge probes found one **opaque white bottom row** (sample 18,
1920 white pixels in the tested central span), despite unchanged before/after
window geometry. This is a remaining transient edge artifact, not a clean seam
result. It could reflect a child placement lag across asynchronous root/child
resize transitions; the capture does not establish the cause. Fixing it without
overriding legitimate child bounds may require Wine window-manager integration.
The second trial had 38 opaque right and 39 opaque bottom probes.

The capture tool now reports alpha coverage and before/after geometry changes;
the edge tool reports coverage independently at each boundary.

The central mean-color metric does not automatically establish sibling ordering.
Some captured frames were visually inspected, but a complete expected-pixel
oracle for every z-order/visibility transition was not run. Neither screenshot
sampling nor successful Present calls exclude a single-frame flash. Use a
native-resolution SCStream/60 Hz recording and renderer/compositor correlation
for that acceptance test.

## Modern Wine experiment

Upstream Wine 11.19 built and installed in a separate experimental directory;
its loader reported `wine-11.19`. The available MSync devel patch failed a dry run
against that source. No MSync-enabled modern engine, optional Wine presentation
interface, or GUI/XR validation of that engine was claimed at initial delivery.
The subsequent [Wine 11.19 preparation](wine-11.19-comparison.md) records corrected
dependencies, boot/crypto/synchronization tests, DXMT fixtures and actual
compositor samples; hardware XR and Steam acceptance remain untested. The primary Wine
snapshot has no Git metadata and was not modified. The independently applicable
interface/implementation plan and build recipe are documented in
[architecture](cross-process-metal-presentation.md) and
[modern Wine/MSync](modern-wine-msync.md).

## Required local acceptance tests

Use the exact build, stage, environment, Retina and capture commands in the
[fixture README](../tests/remote-presentation/README.md), using a fresh prefix.
Then perform the documented Steam sequence with normal out-of-process CEF:
Store/Library navigation independently of resizing; Friends, login, menus,
settings and launch-options dialogs; repeated browser activation, minimization,
popup destruction/recreation, mixed-monitor DPI and human live-drag resize.
Check text/FreeType separately from presentation. None of those Steam scenarios
was run here.

Run the existing SteamVR compositor, Metal sharing/IOSurface and Wine/OpenXR/Monado
setup, plus another OpenXR runtime where available. No XR regression result is
claimed. Test same-process with the setting unset, true DLL unload/reload with
unset/offscreen, and synchronization with MSync disabled in a separate server.
Host mode deliberately pins participating DLLs for process lifetime, so it does
not exercise true dynamic unloading. Invalid/destroyed-window and missing-host
failure branches have source review but not an exhaustive runtime fault matrix.

Remaining design risks include 100 ms geometry polling, a heuristic 250 ms handoff
grace rather than a visibility fence, transient mutex contention, rectangular
clipping, private QuartzCore compatibility, and interleaving remote surfaces with
ordinary native/GDI/OpenGL child content. Root discovery uses Win32 relationships
plus authoritative local Cocoa-window matching; unusual detached Wine window
trees still need deployment validation. Keep the immediate unset/offscreen escape
routes and do not deploy solely on compilation or this fixture's pixel counts.

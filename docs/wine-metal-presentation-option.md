# Optional Wine-managed Metal presentation

CrossOver 26.3 remains the primary engine. `host` preserves the DXMT-managed
remote implementation. This experimental option uses a **separate patched Wine
11.19 build**, with MSync disabled. Neither the CrossOver installation nor the
unpatched 11.19 comparison engine is changed.

## What this implementation provides

`DXMT_CROSS_PROCESS_SWAPCHAIN=wine` requires a supported Wine-managed surface.
`auto` tries Wine first and uses the existing DXMT presentation path only when
Wine's interface or the requested ownership capability is unavailable. An
operational acquisition failure returns `DXGI_ERROR_NOT_CURRENTLY_AVAILABLE`;
it does not install a second backend. Selection remains fixed until destruction.
No environment setting or Wine version number automatically enables this feature.

The optional patch advertises **local Cocoa-root presentation only**, including
local child HWNDs. It deliberately rejects both genuinely foreign child HWNDs
and renderer-owned children with foreign Cocoa roots before allocating resources.
Thus `auto` uses DXMT's hardened CAContext/CALayerHost for Steam's cross-process
CEF windows. `wine` alone cannot yet replace that path for Steam. Vanilla 11.19
has no query export; simply upgrading Wine does not enable this option.

Wine owns its client surface, view, geometry updates and view replacement.
The provider translates the Win32 chain of higher siblings into Cocoa ordering
among participating surfaces. This fixes Wine 11.19's generic update appending
each client view on top regardless of its actual Win32 sibling order. Cocoa
ordering is asynchronous; no Wine window-data lock enters a synchronous Cocoa
call. Mixed Wine-managed and DXMT-managed sibling surfaces and nested ancestor
clipping remain experimental and require additional validation.
DXMT supplies the Metal device, renders directly into Wine's borrowed
CAMetalLayer and drains GPU work before releasing the opaque token. There is
no CPU readback, texture copy, application-level per-frame IPC or per-frame
Wine presentation callback. One callback follows the first real submission;
DXGI_PRESENT_TEST does not reveal the view. Subsequent geometry updates belong
to Wine's client-surface notifications.

The provider has its own client-surface callbacks so window updates can reassert
a submitted surface. Wine's normal GDI flush hides a client view: a one-time
reveal alone produced white content during root resizing in our capture. The
optional patch counts active Metal owners under the window-data lock and leaves
an owned client view visible during GDI flushes. Releasing an outgoing swapchain
cannot clear a successor's count. Registered surfaces detach on HWND destruction;
detached tokens do not modify a reused window's count. After the final owner
releases, ordinary GDI behavior resumes. This changes GDI interoperation only
for applications explicitly using the optional interface; mixed GDI/Metal
content and HWND reuse stress still require further runtime testing.

Replacement surfaces start hidden. Their first submission selects the new
view and hides the previous one; old destruction only clears client_view if it
still refers to that old view. Each view/surface has independent references.
Unlike remote contexts, local views need no remote acknowledgement or grace
period. Submission means queued rendering, not a guarantee that the compositor
has displayed the first pixels.

## Native ABI and optional Wine patch

The generic interface is in `src/winemetal/wine_metal_presentation.h`.
`macdrv_metal_presentation_query_v1(version)` returns a size/version/capability
checked table of create/submitted/release functions. DXMT copies the known
prefix and never appends to or reads beyond macdrv_functions_t. Missing query,
wrong version, short table, missing capabilities or callbacks are unsupported.
An incompatible table is never invoked. Tokens and function pointers stay in
the native Unix process; the appended WineMetal Unix call 157 carries only a
fixed-width object handle, with matching PE64 and WoW64 table entries.

`patches/wine/metal-presentation-v1-wine-11.19.patch` is independently applicable
to the Wine 11.19 source archive. Wine-derived files retain their LGPL licensing;
the original shared ABI header is MIT licensed. The patch adds a win32u
client_surface_register export and a separate winemac query export. It does not
alter Wine's fixed macdrv table or require newer APIs in CrossOver. This is a
fork experiment, not an existing upstream Wine interface or an upstream submission.

The remote Wine CAContextSwapChain/CALayerHost code is untouched and not selected
by this provider. Complete foreign-child ownership, geometry, stacking and
bounded remote context retirement must be implemented and tested before a
future provider advertises remote capabilities. That work belongs inside Wine,
with a new capability contract; runtime discovery does not eliminate Apple's
private API risks.

## Reproduce the separate build

From the DXMT checkout, with the already prepared experimental Wine workspace:

```sh
EXPERIMENTAL=/Users/nickkennedy/Code/wine-crossover/experimental
# Use a new copy. Never patch the vanilla comparison source or CrossOver source.
cp -cR "$EXPERIMENTAL/wine-wine-11.19" "$EXPERIMENTAL/wine-wine-11.19-metal"
patch -d "$EXPERIMENTAL/wine-wine-11.19-metal" -p1 \
  < patches/wine/metal-presentation-v1-wine-11.19.patch
WINE_EXPERIMENTAL_VARIANT=metal tests/remote-presentation/prepare-wine-11.19.sh
ninja -C build
# Use a fresh destination/prefix; staging refuses to overwrite an existing tree.
python3 tests/remote-presentation/stage_wine_metal.py \
  "$EXPERIMENTAL/runtime-11.19" "$EXPERIMENTAL/build-11.19-metal" build \
  "$EXPERIMENTAL/runtime-wine-metal-test" "$EXPERIMENTAL/prefix-wine-metal-test" \
  "$EXPERIMENTAL/deps-x86_64/lib"
DXMT_CROSS_PROCESS_SWAPCHAIN=wine DXMT_REMOTE_LAYER_DEBUG=1 \
  "$EXPERIMENTAL/runtime-wine-metal-test/wine-dxmt-metal" \
  build/tests/remote-presentation/remote-presentation.exe same-siblings
```

The full reproducible build retains `ac_cv_func_pipe2=no`, the explicit x86_64
host/build target and private dependency copies from the vanilla preparation.
For this implementation session the existing vanilla build objects were cloned,
and the modified win32u.so/winemac.so were rebuilt and staged over copies of the
matching vanilla engine. That targeted build and actual execution were tested;
the new full-build variant command is provided but has not been rerun end to end.

The installed experimental launcher is:
`/Users/nickkennedy/Code/wine-crossover/experimental/runtime-11.19-wine-option/wine-dxmt-metal`.
It uses its own prefix, disables MSync, ignores ambient WINEPREFIX and defaults
to `auto`. Use `DXMT_CROSS_PROCESS_SWAPCHAIN=host` to bypass the provider immediately.
It does not inherit the production OpenXR bridge. XR hardware/session and Steam
CEF navigation have not been tested with this option.

## Regression and content validation

Run the focused suite with a fresh disposable prefix:

```sh
python3 tests/remote-presentation/run_wine_backend.py \
  "$EXPERIMENTAL/runtime-11.19-wine-option" \
  build/tests/remote-presentation/remote-presentation.exe \
  "$EXPERIMENTAL/prefix-wine-option-validation" /tmp/wine-option-validation \
  --dependencies "$EXPERIMENTAL/deps-x86_64/lib" --provider
```

Omit `--provider` to test an unpatched engine overlay. The suite checks local
root/child/sibling acquisition, repeated replacement, graceful foreign-root
rejection, `auto` remote root/renderer-owned child/siblings, host termination,
explicit `host` and offscreen. API assertions alone do not establish content.
Use the ScreenCaptureKit harness described in the regression README. Set
`DXMT_TEST_TITLE` to a unique caption when other fixtures are running. Capture
root, child and sibling windows during resizing; inspect pixels and sibling
ordering, not just a black-frame counter. Our white-background fixture exposed
a failure that a near-black metric alone missed.

See `tests/remote-presentation/results/2026-10-09/wine-option` for the measured
results. The static order harness now checks actual colors after asserting HWND_TOP and
HWND_BOTTOM: 20/20 valid red and 20/20 valid green samples respectively. It also
captured churn with 31/40 valid samples and zero blank valid frames. This fixed
a confirmed Wine sibling-order defect; pre-fix HWND_BOTTOM still displayed red.
Geometry-race samples are inconclusive. A small number of sampled
trials cannot prove absence of brief flashes, correct ordering in every frame,
all Retina edge cases or correctness across arbitrary applications. Further
local validation should cover nested children/clipping, mixed-backend sibling order, same-process sibling
paint order after zero/hide/restore, mixed GDI overlays, destruction/HWND reuse,
32-bit Wine, multiple GPUs and changes of monitor scale. For Steam, keep normal
out-of-process CEF and test Store/Library/Friends/login/launch options and
navigation independently of resize. Keep the production prefix unchanged.

Static content ordering can be checked automatically after compiling the capture tool:

```sh
python3 tests/remote-presentation/capture_wine_backend.py \
  "$EXPERIMENTAL/runtime-11.19-wine-option/wine-dxmt-metal" \
  /tmp/dxmt-presentation.exe /tmp/dxmt-capture /tmp/wine-metal-order-validation
```

The harness explicitly requests and asserts Win32 top/bottom order. A back
sibling must produce green central pixels; a front sibling must produce red.
Dynamic resize samples may contain mixed colors; only static scenes carry a
strict color-order assertion. The temporary display-awake assertion is removed
after each scene. Screen Recording permission remains a user-managed setting.

The final packaged sibling run created/released 42 opaque tokens, peaked at
three owned tokens during replacement, and ended with zero remaining. This
measures API ownership rather than asynchronous AppKit object deallocation.
Submission notification and its serialization occur only during establishment
or failed-publication retry; steady-state Present adds no Wine callback, IPC,
copy or extra mutex acquisition for the optional backend.

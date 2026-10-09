# Remote Metal regression procedures

Use a new test prefix and a staged runtime. Do not overwrite the production
CrossOver/SteamVR/OpenXR engine. Commands below are run from the DXMT checkout.

## Build and stage

```sh
ninja -C build src/d3d11/d3d11.dll.postproc src/dxgi/dxgi.dll.postproc \
  src/winemetal/winemetal.dll.postproc src/winemetal/unix/winemetal.so
clang++ -std=c++17 -Wall -Wextra -Werror tests/remote-presentation/rules.cpp \
  -o /tmp/dxmt-remote-rules
/tmp/dxmt-remote-rules
x86_64-w64-mingw32-g++ -std=c++17 -O2 -municode -static \
  tests/remote-presentation/presentation.cpp -o /tmp/dxmt-presentation.exe \
  -ldxgi -ldxguid -lgdi32 -luser32
swiftc -module-cache-path /tmp/dxmt-swift-cache -parse-as-library \
  tests/remote-presentation/capture.swift -o /tmp/dxmt-capture
swiftc -module-cache-path /tmp/dxmt-swift-cache \
  tests/remote-presentation/edges.swift -o /tmp/dxmt-edges
python3 tests/remote-presentation/stage_runtime.py \
  /Users/nickkennedy/Code/wine-crossover/runtime build /tmp/dxmt-test-runtime
mkdir -p /tmp/dxmt-test-prefix
```

The staging script refuses an existing destination and copies the Unix loader
and modules. A symlinked Unix loader can resolve back into the installed engine.
WINEDLLPATH alone is insufficient on the available CrossOver build: the normal
runtime directory takes priority. Verify v2 generation/root/child markers in the
logs and the printed module SHA256s before counting a test.

With Meson `enable_tests=true`, the native rules are also registered as
`remote-presentation-rules`, and a PE harness is built. Existing DX11 tests remain
in place. Direct compilation above avoids changing the main build configuration.

## Run the harness

```sh
export WINEPREFIX=/tmp/dxmt-test-prefix
export DYLD_FALLBACK_LIBRARY_PATH=/Users/nickkennedy/Code/wine-crossover/deps-x86_64/lib
export WINEDLLOVERRIDES='winemenubuilder.exe,mscoree,mshtml=d;d3d11,dxgi=b'
export DXMT_CROSS_PROCESS_SWAPCHAIN=host
export DXMT_REMOTE_LAYER_DEBUG=1
export WINEMSYNC=1
export WINEDEBUG=-all
/tmp/dxmt-test-runtime/lib/wine/x86_64-unix/wine /tmp/dxmt-presentation.exe siblings
```

Run each mode sequentially, not against the same prefix concurrently:

| Mode | Exercise |
| --- | --- |
| `same` | Normal local swapchain; render thread and window in one process. |
| `root` | Renderer process targets host root HWND. |
| `child` | Renderer process targets host-owned child HWND. |
| `owned-child` | Renderer creates its own child of a foreign root; asserts child/source PID match and root PID differs. |
| `siblings` | One renderer presents two children, recreates both contexts and checks that old destruction preserves new generation properties. Moves sibling order; hides/shows and collapses/restores one child. |
| `siblings-static` | Static window/control without context replacement or geometry changes; renderer still submits nonblack frames. |
| `terminate` | Host exits while renderer is active; inspect remaining renderer's final release log. |

Each run lasts roughly 15 seconds, including a retirement drain. Sources clear to
varying nonblack green/red/blue colors. Churn creates a replacement and presents it
before releasing the old swapchain. Geometry runs during churn, not just after a
settle. Parent frames request **client** dimensions 2401x1500 and 2400x1501 using
AdjustWindowRectExForDpi. Log actual root sizes: a display/window-manager limit may
prevent those requested dimensions. The CLI driver does not simulate Cocoa mouse
live-resize, and it does not automatically prove pixels or sibling ordering.

Repeat `same` with DXMT_CROSS_PROCESS_SWAPCHAIN unset. Repeat `child` with
`offscreen`; API success is expected and visible remote output is intentionally
absent. Repeat `child` with `host` and DXMT_REMOTE_LAYER_DISABLE_PRIVATE_API=1:
CreateSwapChain should fail with 0x887a0022, the harness should exit nonzero,
and Wine should not report an abort/fastfail/access violation. Unset the failure
setting immediately after that test. Repeat `siblings` with WINEMSYNC=0 in a
separate prefix/server; do not mix MSync settings on a running wineserver.

Run at least five repeated launches. D3D11 is loaded dynamically only in the
renderer, so the root process in cross-process modes tests DXGI without D3D11.
The renderer releases its DLL reference after retirement; in host mode actual
unload is deliberately prevented by the module pin. Test true unload/reload with
the feature unset/offscreen in a local fixture, and treat host pinning as an
explicit lifecycle policy, not successful unloading.

## Measure content

While a harness run is active, launch the capture command from another terminal:

```sh
/tmp/dxmt-capture 'DXMT remote regression' /tmp/dxmt-captures-trial1 40 \
  > /tmp/dxmt-captures-trial1.csv
```

The helper waits for the matching window, warms up for two seconds, captures
compositor pixels with ScreenCaptureKit, saves PNGs, and reports central RGB means
and near-black flags, alpha coverage, geometry-change and valid-sample flags. It requires macOS 14+ and Screen Recording permission for
the invoking app. Failure to capture is **not** a passing content test. Do not
change privacy settings silently; use an authorized terminal if the environment
cannot capture. The central patch is appropriate for this harness's nonblack
clears, not arbitrary Steam artwork. Capture resolution is explicitly configured
at twice the window's point dimensions; confirm the actual backing scale before
using these files to assert one-device-pixel edge behavior.

A resize between the window metadata snapshot and screenshot can leave transparent
padding around an intact smaller window. Preserve raw near-black counts, but
classify samples with low central alpha coverage or changed geometry as
inconclusive. Report their count separately; never count them as clean frames.
A stable before/after rectangle cannot exclude an intermediate resize.

For Retina tests, enable Retina mode only in the temporary prefix, before starting
the fixture (stop that prefix's wineserver first if it is already running):

```sh
/tmp/dxmt-test-runtime/lib/wine/x86_64-unix/wine reg.exe add \
  'HKCU\Software\Wine\Mac Driver' /v RetinaMode /t REG_SZ /d y /f
/tmp/dxmt-edges /tmp/dxmt-captures-trial1 > /tmp/dxmt-edges-trial1.csv
```

The edge probe counts white fixture-background pixels and reports alpha coverage.
A transparent outer column is inconclusive, not proof of a seamless edge. Inspect
native PNGs alongside actual root size/scale logs and separate valid right and
bottom probes. Screenshot canvas dimensions include the title bar and may round
the Wine client rectangle to Cocoa point boundaries.

Keep a static, unchanged-window control at the same capture rate. Repeat at least
five churn trials and compare black counts to that control. The mean-color metric
cannot prove correct sibling ordering: inspect captured red/green overlap against
the z-order transitions and logs. Probe right and bottom edges at native pixel
resolution for the odd-size tests. Do not infer seams from downscaled thumbnails.
Sampling intervals include capture/PNG overhead, so use recorded timestamps.
A clean 40-sample run cannot exclude a low-rate or single-frame glitch. Use a
60 Hz recording/SCStream for live-drag flashes below screenshot sampling resolution.

Correlate black samples with publication, attachment, retirement, source-release
and live-host logs. The current instrumentation observes Present submission and
host attachment, not a compositor-visible frame fence. It cannot conclusively
attribute every blank frame to renderer inactivity versus compositor latency.

## Steam and XR validation

Use a separate Steam test bottle with the staged engine and matching DLL set.
Remove injected --single-process and --in-process-gpu switches. Record actual
steamwebhelper command lines and GPU process count; do not assume Steam forwarded
a switch. Navigate Store <-> Library at least ten times independently of resizing.
Open Friends, login, menus, settings and launch-options dialogs repeatedly.
Collapse/restore windows and switch active browser trees. Resize both directions,
repeat odd physical client sizes, move across displays with different scale
factors, minimize/restore, and close/reopen popups. Verify text rasterization as
well as artwork; check logs for missing FreeType rather than attributing absent
glyphs to a presentation bug.

Then verify ordinary game swapchains, shared Metal textures/events/IOSurface,
SteamVR CEF UI, the SteamVR compositor, the Wine/OpenXR bridge, and Monado in the
existing setup. Test an additional OpenXR runtime when available. No XR test is
claimed by the harness. Revert to the primary installation if any regression
appears; it is not modified by staging.

## Separate upstream engine

See [Wine 11.19 comparison preparation](../../docs/wine-11.19-comparison.md) for
the isolated build, launchers, crypto/HTTPS/Vulkan smoke executable and matching
OpenXR bridge. This does not change the production CrossOver runtime.

## Optional Wine presentation backend

See [the experimental provider](../../docs/wine-metal-presentation-option.md)
for the independently applicable Wine patch, isolated build/staging and exact
validation commands. `same-child` and `same-siblings` exercise Wine-owned local
children in the fixture process; remote fixtures remain separate processes.
`DXMT_TEST_TITLE` gives captures a unique window caption. The focused
`run_wine_backend.py` suite requires a fresh prefix and checks both the optional
provider and fallback. Its return-code assertions do not certify visible output.

`capture_wine_backend.py` runs explicit `same-siblings-back` (green) and
`same-siblings-front` (red) controls plus sibling churn. Static controls assert
Win32 order before capture and check actual central colors. Dynamic samples
check blank/white content but do not assert every paint transition.

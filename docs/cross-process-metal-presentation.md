# Cross-process Metal presentation in the DXMT fork

## Deployment decision

CrossOver Wine FOSS 26.3 (Wine 11.0, MSync) remains the primary engine. This
implementation requires **no Wine source change**, subsystem backport or newer
client-surface API. `DXMT_CROSS_PROCESS_SWAPCHAIN=host` still selects DXMT's
CAContext/CALayerHost mechanism. The installed engine and the SteamVR, OpenXR,
Monado and Metal sharing paths are not changed by these commits.

This fork's code is AI-assisted with the repository owner's explicit exception
to AGENTS.md for this task. It is not intended as an upstream DXMT contribution.
No code was copied from the comparison patches, and no upstream PR/MR is opened.

## Original failure and investigation

[DXMT issue 141](https://github.com/3Shain/dxmt/issues/141) describes CEF's GPU
process creating a swapchain for a window presented by another process. Wine's
window data and Cocoa windows are process-local. A child's Win32 owner can be
its renderer while its Cocoa root belongs to the browser: child PID alone is
not a sufficient presentation-ownership test.

The old fork already checked NULL window data and had functioning remote
presentation. Source inspection confirmed separate faults in that implementation:

* Destruction unconditionally removed the HWND property, including a successor's
  context. Publication and withdrawal had no interprocess serialization.
* Discovery used the child's PID, recursively called EnumChildWindows although
  it already visits descendants, and removed predecessors before attaching replacements.
* Hosts followed the entire content view and shared a fixed high zPosition.
* The source context was released immediately; retaining only a host cannot
  preserve a context that has been destroyed.
* Missing Metal views aborted the application, and background code could outlive
  an unloaded DLL. A failed CreateThread also leaked its event handle.

These are source-confirmed defects, not a claim that each was independently
reproduced as a visible Steam failure on this machine.

The later issue reports distinguish child geometry, sibling order, genuine
zero-area windows and context churn. See the
[corrected diagnosis](https://github.com/3Shain/dxmt/issues/141#issuecomment-5477055980)
and [resize measurements](https://github.com/3Shain/dxmt/issues/141#issuecomment-5481901941).
Their measured missing glyphs were subsequently traced to FreeType loading.
Their sampling rates and results are not measurements of this branch. The
published Wine/DXMT patches were inspected for comparison, not transplanted.

## DXMT-managed backend

### Presentation ownership

The PE factory validates the HWND and resolves GA_ROOT. It checks both the child
and root PIDs. A foreign root explicitly selects remote creation even if local
child window data exists. For other cases the platform layer tries Wine's existing
local client-view interface, then an exact local WineWindow HWND lookup. If local
presentation is unavailable, the configured remote/offscreen path can be used.
It never substitutes the key window or dereferences missing window data.

Properties remain on the **child**, while host notifications target the root's
process. The receiver independently requires an actual local Cocoa window with
that root HWND before attaching. PID is a discovery hint, not proof of Cocoa
availability. Unavailable roots are retried, not attached to an unrelated window.

### Protocol and replacement invariants

The v2 protocol uses named per-HWND mutexes and fixed-width property values:
context ID, source PID, source process creation time, generation, source extent,
and host acknowledgement. Generations are nonzero and scoped to the publishing
process. The source process creation time prevents PID reuse from being mistaken
for the old source. Window destruction removes the properties; a reused HWND
without the exact publication cannot validate an attachment.

All complete reads and mutations take the same mutex. Generation is the commit
marker, written last. No protocol mutex is held during a Cocoa call. The host
revalidates the publication after native attachment before acknowledging it.
It opens a source process handle and verifies both liveness and creation time.

A source publishes after the first real Present submission, not during the
constructor or DXGI_PRESENT_TEST. The host attaches and places a successor before
removing predecessors for that child. Failed attachments retain the old host and
retry. A retiring swapchain transfers its source-view reference to a retirement
queue. Each child/context has its own acknowledgement and deadline; another
child's replacement does not drain it.

Withdrawal occurs only when PID, context ID and generation still match the
retiring publication, under the named mutex. Releasing A cannot remove B's
publication. Retired sources are retained until a successor has been acknowledged
for 250 ms, the child is destroyed, or a 2 s deadline expires. The final current
publication is withdrawn at expiry. The retained resource is the CAContext itself,
not merely the host. GPU work is drained before transferring the source reference.

**Limits:** acknowledgement means attached, not first visible pixels or GPU
completion. The 250 ms grace is a bounded heuristic, not a compositor fence.
Retention is bounded during normal operation, but protocol-lock starvation or
failure of both thread creation and thread-pool scheduling can delay cleanup.
If both scheduling paths fail, the retiring destructor drains synchronously through
the same grace outside all locks. Exceptional paths are logged and are not proven
by the runtime suite.
A cooperative publisher is assumed: named properties/mutexes are not a security
boundary against malicious processes in the same Wine prefix.

### Geometry and visibility

The source tree remains in swapchain-buffer pixels. Host placement uses the actual
child client rectangle mapped into the root client, intersected with root and
ancestor client bounds. Placement carries source extent separately, so the host
can scale the retained source during a window resize without producer transfers.

The native layer maps root client units to current Cocoa content bounds and
accounts for flipped/unflipped views. This avoids assuming Wine always enables
Retina or always uses a factor of two. Root-edge coordinates map to the actual
content edge, including odd dimensions; interior sibling coordinates use the same
mapping. This can introduce a fractional stretch where Wine rounds its root bounds;
it is not a promise of exact per-pixel alignment for every DPI/region combination.
Backing-property and parent frame/bounds notifications relayout the native host.

Unknown geometry is explicit and initially hidden. A legitimate zero-area child
is hidden, never stretched over the root. Visibility is applied independently of
frame equality, including restoration to the previous dimensions. No host
background color is installed before the remote content exists.

Win32 ancestor/sibling paths are snapshotted and sorted into paint order. Children
paint in front of their parents; a sibling subtree keeps its relative order after
recreation. Placement assigns distinct ordering values rather than one universal
high zPosition. Native hosts are placed above Wine's ordinary content views.

**Limits:** ordering among DXMT remote hosts is implemented; interleaving with
non-DXMT GL/Vulkan/GDI/Metal child surfaces and arbitrary shaped-window regions
needs additional Wine window-manager integration. Clipping is rectangular. Rapid
Win32 movement/visibility/z-order changes can take up to the 100 ms discovery
interval to reach Cocoa. One Retina churn capture retained a white bottom row; asynchronous root/child
updates or polling lag remain possible causes. This branch is not proven
resize-shimmer-free. Mixed-monitor DPI and live dragging still require local
pixel validation. This fallback is not a replacement for Wine's full window manager.

### Threads and loading

D3D11 and DXGI both queue a bootstrap from DLL_PROCESS_ATTACH. The DLL is pinned
before queueing so even a pending callback cannot outlive its code. DllMain does
not create the presentation worker directly, wait for it, or enter Cocoa. The
bootstrap creates the per-process named event and worker outside DllMain. The
first DLL wins host registration; the duplicate closes its event handle.

**Chosen lifecycle:** opt-in host mode pins participating DXGI/D3D11 modules for
the process lifetime. FreeLibrary therefore does not truly unload/reload those
DLLs in host mode. This avoids loader-lock joins and use-after-unload; it is an
explicit limitation, not an orderly-unload implementation. Default/offscreen mode
does not start or pin remote hosts. Windows process termination reclaims the worker
and event. Retired source references have a separate worker, because the DXGI
host worker and D3D11 publisher do not share C++ globals. Source termination is
recognized through its process handle; host termination leaves the renderer able
to retire its source resources without waiting indefinitely for acknowledgement.

The worker scans at 100 ms intervals to handle missed events, delayed DLL loading,
window destruction, and transient attachment failures. EnumChildWindows is called
once per root, without recursive descendant enumeration. A failed enumeration
or contended protocol lock does not falsely withdraw every host.

### Errors and observability

Invalid/destroyed HWNDs return DXGI_ERROR_INVALID_CALL. An unavailable Metal view
or layer returns DXGI_ERROR_NOT_CURRENTLY_AVAILABLE. Buffer initialization and
semaphore failures propagate their HRESULT. The constructor no longer aborts.
Fallible Unix calls bypass the debug-build assertion used for infallible calls.

`DXMT_REMOTE_LAYER_DEBUG=1` enables protocol IDs, source/root/child relationships,
attachment/removal, retirement/release counts, geometry, source extents, visibility,
and order diagnostics. Native messages distinguish private-API unavailability,
CAContext creation exceptions, missing local Cocoa hosts, and attachment exceptions.
Missing host participation is reported by event discovery; discovery continues to
retry. The generic HRESULT does not encode every diagnostic category separately.
Normal presentation sends no per-frame IPC or CPU copies. Geometry polling belongs
to the host, and source metadata changes only on resource establishment/resize.

## Configuration and compatibility

| Setting | Meaning |
| --- | --- |
| unset / other value | Keep the existing default cross-process rejection; ordinary local swapchains still work. |
| `offscreen` | Create a diagnostic Metal target when local presentation is unavailable; no remote host or publication. Successful Present does not mean visible output. |
| `wine` | Experimental Wine-managed local-root/child presentation; requires the optional provider patch and fails gracefully when unsupported. |
| `auto` | Prefer the optional Wine provider for supported local ownership; otherwise use the existing DXMT path before resource acquisition. |
| `host` | Opt into the hardened DXMT CAContext/CALayerHost path on CrossOver 26.3 and compatible Wine engines. |
| `DXMT_REMOTE_LAYER_DEBUG=1` | Enable detailed lifecycle/placement diagnostics. |
| `DXMT_REMOTE_LAYER_DISABLE_PRIVATE_API=1` | Deliberately disable remote private APIs for failure testing; ordinary local presentation remains available. |

The experimental Wine option and `auto` are documented in
[Wine-managed presentation](wine-metal-presentation-option.md). `host` continues
to select DXMT, including on CrossOver 26.3. The optional provider currently
supports local Cocoa roots and children; remote Wine ownership remains future
work. Exactly one backend owns each swapchain. Operational acquisition failures
do not trigger automatic fallback. `auto`, like `host`, pins the host DLLs for
safe process-lifetime fallback hosting.

Install matching DXGI, D3D11, winemetal.dll and winemetal.so together. The appended
Unix calls use fixed-width payloads and matching PE64/WoW64 table entries. Do not
mix a new WineMetal PE DLL with an older Unix library. No Wine macdrv function
table member is added or read beyond the existing supported prefix. PE32 runtime
compatibility remains unverified because the available Wine build is x86_64 only.

Disable the feature immediately by removing DXMT_CROSS_PROCESS_SWAPCHAIN for the
next application launch. MSync selection remains the engine's independent
WINEMSYNC setting. Existing IOSurface/shared texture/event/Mach-port, OpenXR and
SteamVR compositor code is not changed. Those integrations still need runtime
regression testing before deployment.

## Private Apple API boundary

CAContext, CALayerHost and CGSMainConnectionID stay in WineMetal's Unix platform
layer. Classes, selectors and the connection function are validated at runtime;
Objective-C exceptions fail creation/attachment gracefully. No private framework
headers or new hard-coded QuartzCore object layouts are used.

Runtime lookup does **not** make these interfaces public or officially supported.
Apple may change their availability/behavior across macOS releases. Signing,
hardened-runtime, notarization, entitlements and distribution policy must be
validated for the actual packaged application; runtime detection is not App Store
approval or a compatibility guarantee.

IOSurface and Metal shared-texture handles are credible public ways to transport
GPU resources. They do not themselves graft a renderer's layer into a foreign
process's window. A public alternative would need a participating host, resource
IPC, synchronization, and local CAMetalLayer composition/presentation. It might
avoid CPU readback but still introduce GPU copies or a compositor and a frame
protocol. There is no demonstrated equivalent public replacement in this task.
Wine's newer remote-layer path also uses the private QuartzCore classes.

## Wine-managed experimental path

The available Wine 11.0 source snapshot has neither
macdrv_client_surface_acquire_metal_swapchain nor CAContextSwapChain and has no
Git metadata. Backporting that subsystem is outside the primary-engine change.
The separately downloaded upstream Wine 11.19 source has the client-surface
interface but still explicitly rejects cross-process children in window.c.
Simply upgrading Wine does not establish CEF correctness.

A limited provider is now implemented and tested separately; see
[the optional Wine patch and validation](wine-metal-presentation-option.md).
The following plan remains for complete **remote** Wine-managed presentation:

1. Add generic child remote-surface support inside winemac: keep child identity,
   find the realized root, pass child geometry/clip/order through remote-layer
   messages, and update on window-manager transitions.
2. Fix context replacement in Wine before exposing the backend. Retain each
   outgoing client surface/context per child, attach successors before retiring
   hosts, and impose bounded cleanup on destruction/failure. A host reference
   alone is insufficient.
3. Export a separate `macdrv_metal_presentation_query_v1` interface with an exact
   caller size, ABI version, capabilities, and explicit acquire/release methods.
   **This export exists only in our optional patch, not vanilla Wine.** Remote capability
   bits must distinguish root, child, geometry/visibility, stacking, and replacement
   support. Never extend the fixed-size macdrv_functions_t table.
4. Return an opaque surface token and retained CAMetalLayer with a documented
   owner/release pair. Wine owns geometry, hosting and context lifetime; DXMT owns
   Metal rendering. Failed acquisition must leave no published/hosted resources.
5. Resolve the query once by symbol lookup and validate size/version and all
   required capabilities. Missing/partial interfaces select the DXMT fallback
   before acquisition. Post-acquisition failures require complete rollback before
   trying another backend. Do not interpret a Wine version number as capability.
6. Run the same content/geometry/churn suite with both backends and MSync on/off.
   Separate generic Wine functionality from fork-specific DXMT glue for any future
   human-authored upstream proposal. No upstream contribution is made here.

| Concern | DXMT host today | Complete Wine-managed backend |
| --- | --- | --- |
| Primary engine | CrossOver 26.3, no Wine patch | Optional modern engine/interface |
| Host participation | DXGI/D3D11 loaded in root process | Wine driver/window manager |
| Geometry/visibility/order | DXMT polls Win32, maps to Cocoa | Wine window transitions |
| Context lifetime | DXMT v2 protocol and bounded retirement | Wine client surface lifetime |
| Private API risk | QuartzCore remote layers | Still QuartzCore remote layers |
| Pixel transport | Native Metal/CA remote composition | Native Metal/CA remote composition |

When a complete interface is widespread, the DXMT window-property protocol,
EnumWindows polling, event discovery, retirement worker and custom host views can
be removed for that backend. Keep them for legacy engines until support is
intentionally dropped.

## Validation and remaining work

See [regression procedures](../tests/remote-presentation/README.md) and
[recorded results](cross-process-metal-presentation-validation.md). API assertions,
native math tests and builds are separate from actual captured content. Steam's
Store, Library, Friends, login, menus and launch-options dialogs, normal
out-of-process CEF, navigation, human live resize, mixed-monitor DPI, and XR
regressions must be checked before replacing a production installation.

The experimental modern Wine/MSync investigation and reproducible build status
are recorded separately in [modern Wine and MSync](modern-wine-msync.md).

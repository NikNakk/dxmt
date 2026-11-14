# Native macOS sharing metadata

The `macos-xr-native-sharing` development branch exposes the native macOS
representation that DXMT already creates for shared D3D11 resources.

This is intentionally a DXMT interop facility rather than an XR-specific API.

## D3D11 private-data keys

`include/dxmt_native_interop.h` defines:

- `DXMT_GUID_SHARED_TEXTURE_BOOTSTRAP_NAME`
- `DXMT_GUID_SHARED_FENCE_BOOTSTRAP_NAME`

For a shared D3D11 texture, the first key contains the opaque bootstrap name
registered for the texture's existing Metal shared handle.

For a shared D3D11 fence, the second key contains the opaque bootstrap name
registered for the fence's existing Metal shared event.

Both payloads are fixed `DXMT_NATIVE_SHARE_NAME_SIZE` (54-byte) buffers.
Consumers should copy and transport the bytes but must not interpret the name.

## Scope

The implementation does not create an additional texture, perform an
IOSurface copy, or introduce an XR/runtime dependency. It merely publishes
metadata for the native object DXMT already uses to implement D3D11 sharing.

The intended validation is a consumer outside DXMT that creates a shared
Texture2D/Texture2DArray and a shared D3D11 fence, reads both names using
`ID3D11DeviceChild::GetPrivateData`, and reopens the corresponding Metal
objects in a native macOS process.

## Import native resources into D3D11

`ID3D11Device::QueryInterface(DXMT_IID_NATIVE_DEVICE)` returns the DXMT-owned
`IDXMTNativeDevice` extension from `dxmt_native_interop.h`:

```cpp
HRESULT ImportSharedTexture(const char *broker_name,
                            const D3D11_TEXTURE2D_DESC *descriptor,
                            ID3D11Texture2D **texture);
HRESULT ImportSharedEvent(const char *broker_name, ID3D11Fence **fence);
```

These import resources created by a native producer. Texture import wraps the
existing Metal allocation through `Texture::importNative`; it does not allocate
replacement storage, rename the texture or copy image data. Ordinary 2D and
2D arrays preserve their format, size and slice count. The descriptor must use
DEFAULT usage, one mip/sample, no CPU access or misc flags, and render-target
and/or shader-resource bind flags. Native Metal format, dimensions, type,
usage and physical GPU must match; mismatches return `E_INVALIDARG` and no
resource. Depth/other layouts are outside this initial interface's scope.

Event import exposes a native `MTLSharedEvent` as an `ID3D11Fence` usable by
`ID3D11DeviceContext4::Signal`/`Wait` and `GetCompletedValue`. Creating an NT
shared handle from that imported fence is unsupported. The native producer
retains its event, and either side can participate in its timeline.

The names identify **revocable broker endpoints**, using the native-only Mach
protocol in `dxmt_native_capability.h`. They are distinct from the existing
private-data names above, which resolve directly to Metal handles. The Unix
library performs bootstrap lookup, requests the versioned texture/event kind,
validates the reply, reopens the Metal object and releases all temporary Mach
rights. No Mach port or Objective-C pointer crosses the COM interface.

The producer retains the resource and broker until import is complete. Closing
the broker's receive endpoint prevents future imports; wrappers already
imported retain their own Metal objects. The producer must authorize callers
(e.g. kernel audit-token effective UID) and handle failed reply sends without
leaking transferred rights. Existing Wine Unix call numbers are unchanged;
the new texture/event operations append entries 146 and 147.

This API has no OpenXR/runtime dependency. The external `macos-wine-xr` proof
passed three 2D images and three array-of-two images under Wine 8.16 on an Apple
M5 (2026-10-01): D3D11 clear patterns were checked by a native GPU shader;
invalid descriptors and imports after broker closure were rejected. The same
API wrapped native Monado runtime-owned images directly and shared staging
images used for Meta's GPU-copy path. Physical headset/game validation remains
in the consumer project.

## Stock Wine 11.10 presentation

When Wine does not export the macdrv Metal-view helpers, winemetal resolves
the requested top-level HWND through Wine 11.10's `WineWindow.hwnd` selector
on the AppKit thread. It attaches a retained Metal view to that exact window,
autoresizes it with the content view and removes it on release. Mouse hit
testing passes through to Wine's content view. Missing window/class/selector
matches remain unsupported; the fallback never selects an arbitrary key or
foreground window. The existing exported-helper path remains preferred.
This uses Wine's internal Objective-C surface and must be checked when updating
Wine. Child HWND presentation is outside this fallback's scope.

On an Apple M5, the consumer's two-window desktop probe passed create, present,
resize and destruction with stock Wine 11.10 (2026-10-02). The same stack
submitted 328 `hello_xr` frames to simulated Monado with direct runtime-image
sharing and 520 to Meta with shared staging plus a native Metal blit. Native
texture/event import Unix call IDs remain unchanged.


## Protected immediate-context fences (2026-10-10)

`Signal` and `Wait` hold the existing recursive device mutex across their
complete command-stream operations, like other immediate-context operations.
Previously `Signal` emitted its event before taking the lock indirectly in
`Flush`. A thread inside `ID3D11Multithread::Enter` could then flush that event
ahead of its own final texture writes. `Wait` likewise emitted its GPU wait
after releasing the lock taken by `Flush`. The application retains control of
`SetMultithreadProtected`; this change does not enable it globally.

The consumer's standalone Wine/Metal graphics-interop probe uses its unchanged
production DXMT backend and gates the final application clear with a native
Metal event. A matched pre-fix build of `381052e` fails the premature-completion
check for private Metal and IOSurface textures (2 failures). The corrected
D3D11 DLL and its stripped installation artifact pass all probe cases (0
failures), including array and reverse-write checks, with Metal validation
active. Logs live in `macos-wine-xr/docs/results/dxmt-protected-fence-2026-10-10/`.
No Monado service or headset is involved in this test. The change is installed
for fresh local Wine launches; effects on headset checkerboards and pacing
remain unverified, including whether those workloads enable protection.


### Deployment dependency check

The initial consumer deployment replaced D3D11 alone. A subsequent SteamVR
launch aborted on the unavailable `winemetal.dll.WMTMetalViewSubmitted`
presentation export in its older WineMetal DLL. The verified fence defect
remains valid, but a texture-only probe cannot validate presentation ABI.
Build/postprocess and deploy D3D11, DXGI, WineMetal PE and WineMetal native
modules as a matching set. The consumer restored the old D3D11, then validated
the complete new set with ordinary and cross-process child/sibling fixtures,
fixture pixel capture (10 valid samples, no near-black output), and the fence
probe before installing all matching modules. Fresh Steam/SteamVR and headset
confirmation remain pending; the manifest and logs are retained in the
consumer's `docs/results/dxmt-protected-fence-2026-10-10/`.


The subsequent Home hardware follow-up still reports black frames and adds
nonworking dashboard/Steam-logo panels. The consumer has restored its entire
previous D3D11/DXGI/WineMetal presentation set to isolate that regression.
The fence patch remains in this source tree, but is no longer deployed in the
primary engine. Its standalone regression result remains valid; the broader
presentation branch update is not accepted as a hardware-validated replacement.
The consumer driver separately shortens keyed-mutex ownership before runtime
frame commit; fresh application/headset validation remains pending.


Subsequent connection logs show SteamVR was unable to connect to Steam until
Steam was started separately. The consumer's dashboard/logo report therefore
does not establish a regression in this branch's presentation code; rollback
is an isolation measure. Visual corruption reports still precede a fresh
SteamVR launch of the driver's early-handoff change. No panel recovery or
headset synchronization fix is claimed.


A fresh consumer session confirms the early-handoff driver but still has
black frames. Its ReleaseSync calls can take 17.4–193 ms with sub-0.4 ms source
holds, motivating bounded diagnostics in this fork that split context-lock
entry, signal enqueue/Flush, and the remaining owner-release work. Check
`signal_queued` before interpreting the final phase on failing calls.

The consumer now deploys a minimal rebuild of its previous presentation
baseline, `steamvr-in-process` (`756a657`), with only the proven Signal/Wait
locks and those diagnostics. The reproduction patch is
[retained here](patches/steamvr-in-process-sync-diagnostics.patch). Matching
runtime/prefix modules passed the production fence probe, ordinary two-window
presentation and 200 GPU keyed handoffs between two devices in one process.
The baseline fence fix is deployed again; the broader presentation branch
remains out of the primary engine. Fresh headset validation is pending.


### Keyed owner-call phase diagnostics, 2026-10-10

The owner-thread handoff now samples serialization, dispatch/thread creation,
wakeup, time in D3DKMT/Wine and return notification separately. Slow/error logs
include PID/caller thread, shared mutex handle, operation/key, timeout/status
and GetTickCount64 timestamps. Sampling is bounded separately for acquire and
release. No ownership, GPU fence or wait behavior changes. The independently
staged minimal presentation baseline carries these diagnostics via
`docs/patches/steamvr-in-process-sync-diagnostics.patch`.

The final baseline passes controlled timeout followed by release on another
caller thread and reacquisition, plus 200 alternating GPU pixel handoffs. The
production D3D11/Metal protected-fence regression also passes with zero
failures under Metal validation. Monado evidence is saved under
`doc/results/steamvr-source-lock-20261010/`. Hardware phase attribution remains
pending; an acquire duration inside Wine can include a legitimate wait for
another owner and is not, by itself, evidence of Wine RPC overhead.


The current minimal baseline also logs bounded per-ownership intervals:
acquire callback setup, application-owned gap, release signal callback, and
release owner handoff. Tick timestamps, PID and release fence permit matching
to Wine server history. These are wall-clock durations, not GPU timings; the
application gap can include context-lock waits or descheduling. A deliberately
held mutex, timeout recovery, 200 GPU pixel handoffs and the graphics interop
regression pass. Hardware early/late Home stack attribution is still pending.


### Ownership wait attribution, 2026-10-11

The same minimal `756a657` presentation baseline now logs `KeyedMutex owned
waits` at successful releases with app-owned intervals at least 10 ms (first
8, then every 128th per immediate context). Each record carries PID, caller,
shared handle, acquisition-return tick, pre-release tick and release fence.
Snapshots exclude that texture's acquire setup and release-signal Flush.
Device-wide categories measure context/device-lock contention, Flush1 body,
command-chunk capacity, CPU-coherence fence and frame-latency waits. Scopes
run only while at least one keyed texture is owned; uncontended locks do not
read a clock. Context and queue retain shared counter storage through teardown.

Categories overlap: a Flush may include a capacity wait; other threads or
simultaneously owned textures may contribute, and completed wait scopes can
straddle snapshot boundaries. Do not sum categories or call the remaining
wall time CPU rendering. GPU worker completion waits are deliberately absent:
only producer CPU dependencies are measured. Frame-latency attribution is
implemented but was not exercised in the ownership fixture.

An isolated fixture verifies a controlled 35 ms context hold is attributed
as about 38 ms contention, and actual flush/capacity/CPU-fence calls appear
in GPU handoff records. Timeout recovery, cross-thread release, 200 alternating
pixel handoffs and the production graphics interop protected-fence regression
pass. The diagnostic patch applies to baseline 756a657 and reproduces all
eight source files. Seven matched runtime/prefix modules are installed with
backups; WineMetal and the driver hashes are unchanged. Hardware attribution
awaits a fresh launch; hardened cross-process migration remains separate.
Monado stores evidence in `doc/results/dxmt-owned-waits-20261011/`.


### Separate DXGI pacing attribution, 2026-10-11

Owned-wait logs also include `swapchain_latency_ms/calls` for SyncFrame's
per-swapchain fence and `present_ms/calls` for the complete Present1 API call.
The existing frame-latency field remains queue PresentBoundary pacing. The
separate fence is enabled by DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT.
Total Present overlaps context/submission/pacing categories; do not sum them.
Only timing scopes were added to the active fork and installed minimal
baseline swapchain independently, preserving their existing presentation
behavior and keeping the hardening migration separate.

The isolated fixture now presents six times while owning a keyed texture and
records six Present calls/five swapchain waits: 41.642 ms total Present,
22.334 ms separate swapchain waiting and 0.013 ms queue pacing. Timeout recovery,
context contention, 200 GPU handoffs and protected-fence interop all pass.
The baseline builds, its patch reproduces nine source files, and seven matched
modules are installed with backups. A fresh Steam/SteamVR run is needed for
Home attribution; this fixture does not establish Home's black-frame cause.
Monado evidence: `doc/results/dxmt-present-waits-20261011/`.


### Requested hardware attribution test, 2026-10-11 01:03

The user-requested PS VR2 Home test runs about two minutes with the new fields
active and installed hashes verified. Thirty post-initial bounded samples
show median ownership 24.010 ms and median total Present 0.052 ms; separate
swapchain pacing is unused, queue pacing median zero/max 2.747 ms, and other
measured producer waits are tiny. This excludes these DXMT dependencies as
the bulk of the selected gap, not all possible application waits. Compositor
mutex acquisition timeouts persist. Home reports 5879 presents; no visual
observation or physical scanout timing claim is made. The test session is
closed gracefully. Next probe should cover Home/OpenVR work/waits during
ownership. The matched minimal baseline remains installed; hardened migration
is separate. Monado evidence: `doc/results/steamvr-present-headset-20261011-010342/`.

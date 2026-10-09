# Preparing the upstream Wine 11.19 comparison

This is an unpatched upstream Wine 11.19 x86_64 build, without MSync. It lives
under `/Users/nickkennedy/Code/wine-crossover/experimental`. The CrossOver FOSS
26.3 engine, dependencies, prefixes, SteamVR and Monado service are left alone.
It uses the same DXMT build as the fork's presentation regression fixture; it
does not enable a Wine-managed presentation backend.

## Build and deployment details

The initial baseline detected ARM Homebrew GnuTLS while compiling x86_64 code,
and did not find the available x86_64 MoltenVK. The prepared configuration passes
the existing x86_64 libraries explicitly and enables Wine's test executables.
Native dependencies are copied into `experimental/deps-x86_64/lib`, rather than
changing or relying on alterations to the production dependency tree.

Only the copied FreeType and GnuTLS dylib IDs are normalized and ad-hoc signed.
FreeType previously named another checkout; GnuTLS's unversioned ID confused
Wine's SONAME extraction and produced a string containing compatibility-version
text. Generated config now names `libfreetype.6.dylib`, `libgnutls.30.dylib` and
`libMoltenVK.dylib`.

An actual startup crash exposed a separate SDK/runtime mismatch: the supplied
SDK declares `pipe2()` for macOS 27, whereas this macOS 26.6.2 runtime has no
`pipe2` symbol, including under Rosetta. Autoconf's link-only test accepted the
weak import. Wine called its null address in ntdll `server_pipe()` before debug
initialization. An x86_64 `dlsym` probe confirmed the absent symbol. Configuration
sets **`ac_cv_func_pipe2=no`**, selecting Wine's existing `pipe()`/`fcntl()` path.
No Wine source modification or new synchronization backend is involved.

Reproduce the engine build from the DXMT checkout:

```sh
sh tests/remote-presentation/prepare-wine-11.19.sh
```

The script requires the already downloaded `wine-wine-11.19` source and existing
dependency set. It checks VERSION, uses explicit x86_64 build/host triples and
Homebrew Bison, builds and installs only into `experimental/runtime-11.19`.
Configure/build/install logs are retained in `experimental/logs`.

## Matching DXMT and isolated launchers

After building current DXMT, stage it with:

```sh
python3 tests/remote-presentation/stage_wine_comparison.py \
  /Users/nickkennedy/Code/wine-crossover build
```

Staging refuses an existing overlay destination. Move aside only the previous
experimental overlay before restaging; never overwrite
the production engine. The helper copies Unix modules and current DXMT modules,
records DXMT SHA256s, and creates:

* `experimental/bin/wine-11.19` — baseline upstream Wine, default prefix
  `experimental/prefix-11.19`.
* `experimental/bin/wine-11.19-dxmt` — DXMT overlay, default prefix
  `experimental/prefix-11.19-dxmt`, remote mode `host`.

Both launchers force `WINEMSYNC=0`, select their corresponding Wine loader/server
and private dependency path, disable automatic Wine desktop integration, and
clear an inherited WINEDLLPATH. An ambient WINEPREFIX is deliberately ignored.
Use **`WINE_1119_PREFIX=/absolute/path/to/a/separate/test-prefix`** to override
the test prefix explicitly. Do not point it at a working CrossOver bottle.
WINEDLLOVERRIDES can be overridden explicitly. To disable remote hosting in the
DXMT launcher, use `DXMT_CROSS_PROCESS_SWAPCHAIN=''`; `offscreen` is also available.

## Prerequisite and presentation checks

```sh
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static \
  -I/opt/homebrew/include tests/remote-presentation/engine-smoke.cpp \
  -o /tmp/dxmt-engine-smoke.exe -lbcrypt -lwinhttp
WINEDEBUG=-all /Users/nickkennedy/Code/wine-crossover/experimental/bin/wine-11.19 \
  wineboot -u
WINEDEBUG=-all /Users/nickkennedy/Code/wine-crossover/experimental/bin/wine-11.19 \
  /tmp/dxmt-engine-smoke.exe
WINEDEBUG=-all DXMT_REMOTE_LAYER_DEBUG=1 \
  /Users/nickkennedy/Code/wine-crossover/experimental/bin/wine-11.19-dxmt \
  /tmp/dxmt-presentation.exe siblings
```

The smoke executable checks an AES known-answer vector, performs a
certificate-validated HTTPS HEAD request to Steam, and creates a Vulkan instance
and enumerates physical devices. It does not disable TLS validation. HTTPS
success is a prerequisite, not a Steam login/UI test. Follow the
[presentation procedures](../tests/remote-presentation/README.md) for compiling
the fixture, content capture, Retina and navigation acceptance.

The readiness runner executes boot, engine smoke, targeted Wine suites, OpenXR
module loading and five presentation modes, retaining results in `experimental/logs`:

```sh
python3 tests/remote-presentation/run_wine_readiness.py \
  /Users/nickkennedy/Code/wine-crossover \
  /tmp/dxmt-engine-smoke.exe /tmp/dxmt-presentation.exe
```

Run Wine's targeted tests independently against this same engine, with a distinct test prefix:

```sh
WINE_1119_PREFIX=/Users/nickkennedy/Code/wine-crossover/experimental/prefix-unit-tests \
  WINEDEBUG=-all \
  /Users/nickkennedy/Code/wine-crossover/experimental/bin/wine-11.19 \
  /Users/nickkennedy/Code/wine-crossover/experimental/build-11.19/dlls/ntdll/tests/x86_64-windows/ntdll_test.exe sync
WINE_1119_PREFIX=/Users/nickkennedy/Code/wine-crossover/experimental/prefix-unit-tests \
  WINEDEBUG=-all \
  /Users/nickkennedy/Code/wine-crossover/experimental/bin/wine-11.19 \
  /Users/nickkennedy/Code/wine-crossover/experimental/build-11.19/dlls/bcrypt/tests/x86_64-windows/bcrypt_test.exe bcrypt
```

## OpenXR boundary

The existing in-process bridge was rebuilt in `experimental/openxr-11.19` using
Wine 11.19's SDK and source headers. The bridge and original native OpenXR loader
remain separate from upstream Wine; no bridge code is added to Wine's source.
Build command (requires the existing SDK/bridge checkouts):

```sh
DXMT_SOURCE_DIR=/Users/nickkennedy/Code/dxmt \
MWXR_WINE_SDK=/Users/nickkennedy/Code/wine-crossover/experimental/runtime-11.19 \
MWXR_WINE_SOURCE=/Users/nickkennedy/Code/wine-crossover/experimental/wine-wine-11.19 \
MWXR_WINE_RUNTIME=/Users/nickkennedy/Code/wine-crossover/experimental/runtime-11.19 \
MWXR_NATIVE_LOADER=/Users/nickkennedy/Code/macos-wine-xr/build-in-process/openxr-loader-x64/src/loader/libopenxr_loader.dylib \
OPENXR_SOURCE_DIR=/Users/nickkennedy/Code/OpenXR-SDK \
MWXR_IN_PROCESS_BUILD=/Users/nickkennedy/Code/wine-crossover/experimental/openxr-11.19 \
  zsh /Users/nickkennedy/Code/macos-wine-xr/scripts/build-in-process-gate.zsh
```

To include these rebuilt modules when creating the fresh DXMT overlay, add
`--openxr-build /Users/nickkennedy/Code/wine-crossover/experimental/openxr-11.19`
to the staging command. The native module's ntdll dependency is resolved to the
staged Wine 11.19 ntdll, not a CrossOver binary. Run engine-smoke with
`--openxr-loader` using the DXMT launcher to check DLL/native Unix initialization
without creating an XR instance or acquiring hardware.

Actual XR testing still needs an explicit native runtime manifest, the matching
x86_64 runtime/client libraries, a separately selected ActiveRuntime registry
entry and an isolated service where appropriate. Do not copy CrossOver-linked
Unix XR modules into this engine or run the existing gate launcher unchanged:
that launcher hardcodes another project's prefix. The current bridge prototype's
OpenXR version/capability limits are inherited, not fixed by upgrading Wine.

## Fair comparison

Use CrossOver 26.3 with MSync on, CrossOver with MSync off, and upstream 11.19
without MSync. Use separate prefixes and servers, identical DXMT modules and
settings, the same application version/assets, Retina mode and monitor setup.
Record all engine/module hashes. Stop the **test prefix's** server between
synchronization configurations; do not kill another bottle's wineserver.

First compare correctness: content, navigation, placement, visibility, context
churn and repeated launch/termination. Then compare CPU/frame times and stalls
after warmup over repeated trials. Do not benchmark during compilation or infer
performance from a short color-clear fixture. Wine engine differences beyond
MSync are intentional in the third configuration and must not be attributed
solely to synchronization.

This build is x86_64 only; 32-bit applications/WoW64 are not a validated capability.
GStreamer, SDL, USB, Kerberos and other optional subsystems remain configured as
in the initial comparison baseline. Test actual Steam/CEF, games and XR before
using it as a replacement engine. Preparation does not install or log into Steam,
alter the working Steam installation, or constitute a performance comparison.

## Recorded readiness results — 2026-10-09

After the pipe2 correction, the rebuilt/installed baseline and DXMT overlay both
ran successfully. All ten readiness-runner cases returned zero without timeout:

| Check | Observed result |
| --- | --- |
| Wineboot in new upstream prefix | Exit 0 |
| AES known-answer encryption | PASS, NTSTATUS 0 |
| Certificate-validated Steam HTTPS HEAD | PASS, HTTP 200 |
| Vulkan instance and physical-device enumeration | PASS, Apple M5, one device |
| ntdll synchronization suite | 433 checks, zero failures, one marked TODO |
| bcrypt suite | 23,507 checks, zero failures, eight marked TODO |
| DXMT-overlay prerequisite smoke | AES/HTTPS/Vulkan passed again |
| Rebuilt OpenXR bridge DLL/native initialization | PASS; no XR instance/session created |
| DXMT same/root/renderer-owned child/siblings/termination fixtures | All exit 0 |

The OpenXR module check is part of the DXMT smoke case, and the five fixture
modes are individual runner cases. The tests are not a complete Wine test-suite
run. First-prefix initialization emitted a missing syswow64 rundll32 warning;
this build remains explicitly 64-bit-only.

Three initial ScreenCaptureKit attempts (churn, static control, repeated churn)
failed before capturing their first frame with SCStreamErrorDomain -3811. They
are recorded as failures, not clean-content trials. After a temporary
`caffeinate -u -d -t 60` display-awake assertion, both static and churn captures
succeeded. The correlation indicates display/session state affected capture;
it does not establish a Wine presentation defect or definitively identify the
capture framework's internal failure. No privacy or persistent power setting
was changed; the assertion process was terminated after each trial.

The static control captured 40 valid samples with zero near-black central
patches. Retina churn captured 40 samples, 29 valid and 11 inconclusive because
of geometry changes/coverage; there were zero near-black patches, including zero
among valid samples. Logs reached root client sizes 2401×1500 and 2400×1501.
The edge helper found no white fixture-background pixels; 39 right and 39 bottom
probes were opaque. Transparent probes are inconclusive. This is one churn trial,
not a proof of shimmer-free resizing, sibling correctness at every transition,
or an improvement over CrossOver's earlier trials.

Compact raw results, the artifact hash manifest, capture CSVs, failure records and
suite summaries are retained in
[results/wine-11.19](../tests/remote-presentation/results/2026-10-09/wine-11.19).
Full logs and PNGs remain in the experimental directory. Existing source geometry,
protocol rules and Meson's remote-presentation-rules were also rerun and passed.

**Ready for controlled 64-bit engine comparison.** Actual Steam installation,
login/CEF navigation, SteamVR, XR instance/session/swapchain/hardware tests and
performance comparisons have not been run on this engine. Keep CrossOver as the
working installation until those acceptance tests pass. The existing DXMT
fallback's polling/lifetime/private-API limits still apply; preparing Wine does
not resolve them.

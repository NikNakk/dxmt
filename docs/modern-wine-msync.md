# Separate modern Wine and MSync experiment

The working CrossOver FOSS 26.3 / Wine 11.0 runtime remains installed at
`/Users/nickkennedy/Code/wine-crossover/runtime`. No files in that installation or
its source snapshot were changed. The source snapshot has no Git metadata, so
there is no Wine-side branch pretending to contain tested presentation changes.

## Source comparison

CodeWeavers' current snapshot includes ntdll Unix and wineserver MSync code,
WINEMSYNC selection, shared synchronization state, a Mach message pump, semaphore
pooling, and private ulock/Mach bootstrap entry points. These are changes to both
client and server, not a standalone dylib that can be swapped into upstream Wine.
The local runtime already carries independent MSync leak-related backups; this
task does not overwrite that work.

[marzent/wine-msync](https://github.com/marzent/wine-msync) publishes separate
CrossOver 22/23, staging, and devel patch sets. Its
[README](https://github.com/marzent/wine-msync/blob/main/README.md) explains the
Mach semaphore/shared-state design and WINEMSYNC controls. CrossOver-specific
patches are not suitable as an assumed upstream baseline. The available
`msync-devel.patch` identifies a 2024 patch commit and includes generated protocol
files and many server object-operation changes. A modern port must regenerate
protocol code rather than blindly transplant generated diffs.

Upstream Wine 11.19's server object operations include `get_sync`, and its
synchronization architecture differs from the published patch's expected context.
On 2026-10-09 a **dry run**, without changing the source, rejected numerous hunks
in ntdll, server event/mutex/semaphore/thread handling, object headers, and generated
protocol files. The devel patch is therefore not a directly applicable Wine 11.19
MSync build. This is a demonstrated patch-application failure, not a statement
that MSync cannot be ported to modern Wine.

## Isolated workspace and provenance

Experimental files are under:

* `wine-crossover/experimental/wine-wine-11.19` — upstream wine-11.19 tag source,
  downloaded from wine-mirror's tag archive; no MSync patch applied.
* `wine-crossover/experimental/msync-devel.patch` — published LGPL patch,
  downloaded for comparison; Git blob ID
  `7ec0520965e28517b3eddd4b4930e88824300a71` in the fetched repository listing.
* `wine-crossover/experimental/build-11.19` — separate upstream baseline build.
* Intended installation: `wine-crossover/experimental/runtime-11.19`.

The tag tarball is `/tmp/dxmt-wine-11.19.tar.gz`; application dry-run output is
`/tmp/dxmt-msync-apply.log`. No upstream repositories were modified and no MR was
opened. This baseline is **not an MSync-enabled engine**.

## Build recipe

On this Apple Silicon host, configure must use explicit x86_64 build/host triples.
Simply specifying `CC='clang -arch x86_64'` leaves config.guess selecting aarch64,
which produced conflicting ARM64/x86_64 WinNT definitions in build tools. Homebrew
Bison must precede Apple's old Bison in PATH. The existing x86_64 dependencies
retain their previous absolute install names; the fallback library path is needed
by the resulting Wine runtime. Apple's SIP strips DYLD variables when invoking
system make/recipe shells, so the newly built sfnt2fon needed its FreeType load
command corrected to the existing dependency directory. The primary dependency
library and engine were not modified.

From the separate build directory:

```sh
export PATH=/opt/homebrew/opt/bison/bin:$PATH
export DYLD_FALLBACK_LIBRARY_PATH=/Users/nickkennedy/Code/wine-crossover/deps-x86_64/lib
export CC='clang -arch x86_64'
export CXX='clang++ -arch x86_64'
../wine-wine-11.19/configure --build=x86_64-apple-darwin \
  --host=x86_64-apple-darwin \
  --prefix=/Users/nickkennedy/Code/wine-crossover/experimental/runtime-11.19 \
  --enable-archs=x86_64 --disable-tests --without-x --with-freetype \
  --without-gstreamer --without-sdl --without-cups --without-sane \
  --without-usb --without-v4l2 --without-pcap --without-krb5 \
  --without-gphoto --without-capi --without-netapi --without-opencl --without-ffmpeg \
  FREETYPE_CFLAGS=-I/opt/homebrew/include/freetype2 \
  FREETYPE_LIBS='-L/Users/nickkennedy/Code/wine-crossover/deps-x86_64/lib -lfreetype'
make -j8
# If sfnt2fon cannot load the old FreeType install name, correct ONLY this
# experimental build tool, then resume the build:
install_name_tool -change \
  /Users/nickkennedy/Code/monado-2/.build/crossover-26.3/deps-x86_64/lib/libfreetype.6.dylib \
  /Users/nickkennedy/Code/wine-crossover/deps-x86_64/lib/libfreetype.6.dylib \
  tools/sfnt2fon/sfnt2fon
make -j8
make install
../runtime-11.19/bin/wine --version
```

Configuration, the full upstream build, and installation into the separate
runtime-11.19 prefix completed. The installed loader returned `wine-11.19` from
`--version`. No GUI, Steam, OpenXR or synchronization runtime suite was run on this
baseline. See the validation report for the exact scope.
The configured baseline lacks GnuTLS and Vulkan/MoltenVK development detection;
it is not suitable for Steam login/OpenXR deployment as configured. Do not replace
the primary engine with it. Add explicit x86_64 dependency flags and enable tests
in a subsequent validation build before evaluating Steam, XR or performance.
Do not install a partially built engine over CrossOver.

## MSync port acceptance plan

1. Pin an upstream tag and patch-set revision in a separate Git workspace. Port
   current CodeWeavers or marzent semantics to the upstream `get_sync`/object
   lifetime machinery, not merely failed patch contexts. Review licenses and
   maintain attribution on copied LGPL source.
2. Regenerate Wine server protocol/dispatch files using that source revision's
   generator. Keep wineserver and ntdll from the same build. Preserve wait-any,
   wait-all, abandoned mutex, APC/alertable wait, timeout, pulse-event and handle
   closure semantics, including process termination and object index reuse.
3. Review semaphore pooling, Mach send-right disposal, and server node cleanup.
   The primary engine's prior leak fixes need explicit comparison, not automatic
   application to a different server architecture.
4. Build with both WINEMSYNC=0 and 1, in distinct prefixes and servers. Run ntdll
   synchronization tests, cross-process stress tests, leak/port-count checks,
   repeated launch/termination tests, and long Steam/XR sessions. A startup
   'msync: up and running' line proves selection, not synchronization correctness.
5. Measure CPU-bound and GPU-bound workloads, Metal submission latency and long
   stalls. Do not transfer historical patch README benchmarks to this machine.
6. Only after the engine is validated, add and test the optional versioned Wine
   Metal presentation interface described in the presentation architecture doc.
   OpenXR support must remain independent of the selected presentation backend
   and must not assume that Monado is the only runtime.

A working modern MSync build and a Wine-managed child-presentation backend are
remaining experimental work, not delivered/validated capabilities of this branch.

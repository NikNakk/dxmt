#!/bin/sh
# Rebuild the separate, unpatched upstream baseline; never install over CrossOver.
set -eu
workspace=${WINE_WORKSPACE:-/Users/nickkennedy/Code/wine-crossover}
experimental="$workspace/experimental"
source_tree="$experimental/wine-wine-11.19"
build_tree="$experimental/build-11.19"
runtime="$experimental/runtime-11.19"
dependencies="$experimental/deps-x86_64/lib"
test "$(cat "$source_tree/VERSION")" = 'Wine version 11.19'
mkdir -p "$build_tree" "$experimental/logs"
python3 - "$workspace/deps-x86_64/lib" "$dependencies" <<'PY'
import sys, shutil
from pathlib import Path
source, destination = map(lambda x: Path(x).resolve(), sys.argv[1:])
if source == destination or source in destination.parents or destination in source.parents:
    raise SystemExit('Dependency copies must be separate')
if not destination.exists():
    shutil.copytree(source, destination, symlinks=True)
PY
# Only these experimental copies are changed. The old FreeType ID names another
# checkout; the unversioned GnuTLS ID confuses Wine's SONAME parser.
install_name_tool -id @rpath/libfreetype.6.dylib "$dependencies/libfreetype.6.dylib"
install_name_tool -id @rpath/libgnutls.30.dylib "$dependencies/libgnutls.30.dylib"
codesign --force --sign - "$dependencies/libfreetype.6.dylib"
codesign --force --sign - "$dependencies/libgnutls.30.dylib"
export PATH="/opt/homebrew/opt/bison/bin:/opt/homebrew/bin:$PATH"
export CC='clang -arch x86_64'
export CXX='clang++ -arch x86_64'
export DYLD_FALLBACK_LIBRARY_PATH="$dependencies"
# The supplied SDK weak-links macOS 27's pipe2 on this macOS 26 host.
# Autoconf's link-only probe accepts it, but the runtime address is NULL.
# Use Wine's existing pipe+fcntl fallback, also safe on older deployment targets.
export ac_cv_func_pipe2=no
cd "$build_tree"
"$source_tree/configure" --build=x86_64-apple-darwin --host=x86_64-apple-darwin \
  --prefix="$runtime" --enable-archs=x86_64 \
  --without-x --with-freetype --with-gnutls --with-vulkan \
  --without-gstreamer --without-sdl --without-cups --without-sane \
  --without-usb --without-v4l2 --without-pcap --without-krb5 \
  --without-gphoto --without-capi --without-netapi --without-opencl --without-ffmpeg \
  GNUTLS_CFLAGS=-I/opt/homebrew/include GNUTLS_LIBS="-L$dependencies -lgnutls" \
  FREETYPE_CFLAGS=-I/opt/homebrew/include/freetype2 FREETYPE_LIBS="-L$dependencies -lfreetype" \
  LDFLAGS="-L$dependencies -Wl,-rpath,$dependencies" > "$experimental/logs/configure.log" 2>&1
make -j"${WINE_BUILD_JOBS:-8}" > "$experimental/logs/build.log" 2>&1
make install > "$experimental/logs/install.log" 2>&1
"$runtime/bin/wine" --version

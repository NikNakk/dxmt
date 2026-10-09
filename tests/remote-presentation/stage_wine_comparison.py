#!/usr/bin/env python3
"""Create explicit, isolated upstream launchers and a matching DXMT overlay."""
import argparse
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

p = argparse.ArgumentParser()
p.add_argument("workspace", type=Path)
p.add_argument("dxmt_build", type=Path)
p.add_argument("--openxr-build", type=Path)
a = p.parse_args()
experimental = a.workspace.resolve() / "experimental"
runtime = experimental / "runtime-11.19"
overlay = experimental / "runtime-11.19-dxmt"
dependencies = experimental / "deps-x86_64/lib"
if subprocess.check_output([str(runtime / "bin/wine"), "--version"], text=True).strip() != "wine-11.19":
    p.error("expected the separately built Wine 11.19 runtime")
subprocess.run([sys.executable, str(Path(__file__).with_name("stage_runtime.py")),
                str(runtime), str(a.dxmt_build.resolve()), str(overlay)], check=True)
if a.openxr_build:
    for arch, name in [("x86_64-windows", "wineopenxr.dll"), ("x86_64-unix", "wineopenxr.so")]:
        target = overlay / "lib/wine" / arch / name
        if target.is_symlink():
            target.unlink()
        shutil.copy2(a.openxr_build / arch / name, target)
    # Resolve the staged bridge against this engine's ntdll, never CrossOver's.
    module = overlay / "lib/wine/x86_64-unix/wineopenxr.so"
    subprocess.run(["install_name_tool", "-change", "@rpath/ntdll.so",
                    str(overlay / "lib/wine/x86_64-unix/ntdll.so"), str(module)], check=True)
    subprocess.run(["codesign", "--force", "--sign", "-", str(module)], check=True)

bin_dir = experimental / "bin"
bin_dir.mkdir(exist_ok=True)
for suffix, tree in [("", runtime), ("-dxmt", overlay)]:
    launcher = bin_dir / ("wine-11.19" + suffix)
    prefix = experimental / ("prefix-11.19" + suffix)
    text = "#!/bin/sh\nset -eu\n"
    # Ignore an ambient production WINEPREFIX. A custom comparison prefix must
    # be requested with the distinct WINE_1119_PREFIX variable.
    text += "export WINEPREFIX=${WINE_1119_PREFIX:-" + shlex.quote(str(prefix)) + "}\n"
    text += 'export WINEARCH=win64\nexport WINEMSYNC=0\n'
    text += 'unset WINEDLLPATH WINELOADERNOEXEC\n'
    text += "export WINELOADER=" + shlex.quote(str(tree / "bin/wine")) + "\n"
    text += "export WINESERVER=" + shlex.quote(str(tree / "bin/wineserver")) + "\n"
    text += "export DYLD_FALLBACK_LIBRARY_PATH=" + shlex.quote(str(tree / "lib/wine/x86_64-unix") + ":" + str(dependencies)) + "\n"
    overrides = "winemenubuilder.exe,mscoree,mshtml=d" + (";d3d11,dxgi=b" if suffix else "")
    text += "export WINEDLLOVERRIDES=${WINEDLLOVERRIDES-" + shlex.quote(overrides) + "}\n"
    if suffix:
        text += 'export DXMT_CROSS_PROCESS_SWAPCHAIN=${DXMT_CROSS_PROCESS_SWAPCHAIN-host}\n'
    text += 'mkdir -p "$WINEPREFIX"\n'
    text += "exec " + shlex.quote(str(tree / "bin/wine")) + ' "$@"\n'
    launcher.write_text(text)
    launcher.chmod(0o755)
    print("launcher:", launcher, "default prefix:", prefix)

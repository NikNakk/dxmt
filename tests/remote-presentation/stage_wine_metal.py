#!/usr/bin/env python3
"""Stage the optional provider over a vanilla 11.19 runtime, in a new tree."""
import argparse
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

p = argparse.ArgumentParser()
p.add_argument("runtime", type=Path)
p.add_argument("wine_build", type=Path)
p.add_argument("dxmt_build", type=Path)
p.add_argument("destination", type=Path)
p.add_argument("prefix", type=Path)
p.add_argument("dependencies", type=Path)
a = p.parse_args()
if subprocess.check_output([str(a.runtime.resolve() / "bin/wine"), "--version"], text=True).strip() != "wine-11.19":
    p.error("the provider patch requires the matching vanilla Wine 11.19 engine")
if a.prefix.exists():
    p.error("use a new disposable prefix")
# stage_runtime refuses to overwrite or overlap the input engine.
subprocess.run([sys.executable, str(Path(__file__).with_name("stage_runtime.py")),
                str(a.runtime), str(a.dxmt_build), str(a.destination)], check=True)
for relative in ("dlls/winemac.drv/winemac.so", "dlls/win32u/win32u.so"):
    target = a.destination / "lib/wine/x86_64-unix" / Path(relative).name
    shutil.copy2(a.wine_build / relative, target)
    subprocess.run(["codesign", "--force", "--sign", "-", str(target)], check=True)
# Unix Wine modules resolve their @rpath counterparts from this staged engine.
runtime = a.destination.resolve()
launcher = runtime / "wine-dxmt-metal"
text = "#!/bin/sh\nset -eu\n"
for key, value in dict(WINEPREFIX=str(a.prefix.resolve()), WINEARCH="win64", WINEMSYNC="0",
                       WINELOADER=str(runtime / "bin/wine"), WINESERVER=str(runtime / "bin/wineserver"),
                       DYLD_FALLBACK_LIBRARY_PATH=str(runtime / "lib/wine/x86_64-unix") + ":" + str(a.dependencies.resolve()),
                       WINEDLLOVERRIDES="winemenubuilder.exe,mscoree,mshtml=d;d3d11,dxgi=b").items():
    text += "export " + key + "=" + shlex.quote(value) + "\n"
text += 'unset WINEDLLPATH WINELOADERNOEXEC\n'
text += 'export DXMT_CROSS_PROCESS_SWAPCHAIN=${DXMT_CROSS_PROCESS_SWAPCHAIN-auto}\n'
text += 'mkdir -p "$WINEPREFIX"\n'
text += "exec " + shlex.quote(str(runtime / "bin/wine")) + ' "$@"\n'
launcher.write_text(text)
launcher.chmod(0o755)
print("launcher:", launcher)

#!/usr/bin/env python3
"""Stage DXMT over a separate Wine runtime. Never edits the source engine."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil

parser = argparse.ArgumentParser()
parser.add_argument("wine_runtime", type=Path)
parser.add_argument("dxmt_build", type=Path)
parser.add_argument("destination", type=Path)
args = parser.parse_args()
source = args.wine_runtime.resolve()
target = args.destination.resolve()
if target == source or source in target.parents or target in source.parents:
    parser.error("destination must be separate from the primary Wine runtime")
if target.exists():
    parser.error("use a new empty destination to avoid overwriting another engine")
for directory, _, files in os.walk(source):
    relative = Path(directory).relative_to(source)
    dest = target / relative
    dest.mkdir(parents=True, exist_ok=True)
    for name in files:
        original = Path(directory) / name
        leaf = dest / name
        # Wine resolves real paths when discovering its own loader/DLL root.
        # Unix modules must be copied, not symlinked back into the primary tree.
        if relative.parts[:3] == ("lib", "wine", "x86_64-unix") or relative == Path("bin") and name == "wine":
            shutil.copy2(original, leaf)
        else:
            leaf.symlink_to(original)
modules = {
    "src/d3d11/d3d11.dll": "x86_64-windows/d3d11.dll",
    "src/dxgi/dxgi.dll": "x86_64-windows/dxgi.dll",
    "src/winemetal/winemetal.dll": "x86_64-windows/winemetal.dll",
    "src/winemetal/unix/winemetal.so": "x86_64-unix/winemetal.so",
}
for build_relative, runtime_relative in modules.items():
    original = args.dxmt_build / build_relative
    leaf = target / "lib/wine" / runtime_relative
    if leaf.is_symlink():
        leaf.unlink()
    shutil.copy2(original, leaf)
    print(hashlib.sha256(leaf.read_bytes()).hexdigest(), leaf)
print("loader:", target / "lib/wine/x86_64-unix/wine")

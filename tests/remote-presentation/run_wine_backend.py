#!/usr/bin/env python3
"""Exercise a staged engine only; never inherit the user's production prefix."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument("runtime", type=Path, help="separate DXMT overlay, not primary engine")
p.add_argument("executable", type=Path)
p.add_argument("prefix", type=Path, help="new disposable prefix")
p.add_argument("results", type=Path)
p.add_argument("--dependencies", type=Path, required=True)
p.add_argument("--provider", action="store_true")
a = p.parse_args()
if a.prefix.exists():
    p.error("use a new disposable prefix")
a.results.mkdir(parents=True, exist_ok=True)
env = {k: v for k, v in os.environ.items()
       if not k.startswith(("WINE", "DXMT", "MWXR", "XR_", "DYLD_"))}
env.update(WINEPREFIX=str(a.prefix.resolve()), WINEARCH="win64", WINEMSYNC="0",
           WINELOADER=str(a.runtime.resolve() / "bin/wine"),
           WINESERVER=str(a.runtime.resolve() / "bin/wineserver"), WINEDEBUG="-all",
           WINEDLLOVERRIDES="winemenubuilder.exe,mscoree,mshtml=d;d3d11,dxgi=b",
           DYLD_FALLBACK_LIBRARY_PATH=str(a.runtime.resolve() / "lib/wine/x86_64-unix") + ":" + str(a.dependencies.resolve()),
           DXMT_REMOTE_LAYER_DEBUG="1")
loader = str(a.runtime.resolve() / "bin/wine")
server = str(a.runtime.resolve() / "bin/wineserver")
results = []

def run(name, args, mode, expected=0, timeout=50):
    env["DXMT_CROSS_PROCESS_SWAPCHAIN"] = mode
    with (a.results / (name + ".log")).open("w") as log:
        proc = subprocess.Popen([loader, *args], env=env, stdout=log, stderr=log)
        timed_out = False
        try:
            code = proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            proc.kill()
            subprocess.run([server, "-k"], env=env, check=False)
            code = proc.wait()
    output = (a.results / (name + ".log")).read_text(errors="replace")
    if args and args[-1] == "terminate" and not timed_out:
        deadline = time.monotonic() + 10
        while "failures=0" not in output and time.monotonic() < deadline:
            time.sleep(0.2)
            output = (a.results / (name + ".log")).read_text(errors="replace")
    valid = not timed_out and code == expected
    if args and args[0] == str(a.executable.resolve()):
        if expected:
            valid &= "887a0022" in output  # graceful unavailable HRESULT
        else:
            valid &= "failures=0" in output
            if a.provider and mode == "wine":
                valid &= "Wine client surface HWND" in output
                valid &= "RemoteLayer: publish" not in output
            if mode == "auto" and args[-1] in ("root", "owned-child", "siblings"):
                valid &= "RemoteLayer: publish" in output
    result = dict(name=name, mode=mode, code=code, expected=expected, timed_out=timed_out, assertions_passed=valid)
    results.append(result)
    print(json.dumps(result), flush=True)

try:
    run("boot", ["wineboot", "-u"], "host", timeout=120)
    if not results[-1]["assertions_passed"]:
        raise SystemExit("isolated prefix initialization failed")
    if a.provider:
        for fixture in ("same", "same-child", "same-siblings"):
            run("wine-" + fixture, [str(a.executable.resolve()), fixture], "wine")
        run("wine-foreign-rejected", [str(a.executable.resolve()), "root"], "wine", expected=1)
    else:
        run("wine-missing-rejected", [str(a.executable.resolve()), "same"], "wine", expected=1)
    for fixture in ("same", "root", "owned-child", "siblings", "terminate"):
        run("auto-" + fixture, [str(a.executable.resolve()), fixture], "auto")
    run("host-root", [str(a.executable.resolve()), "root"], "host")
    run("offscreen-root", [str(a.executable.resolve()), "root"], "offscreen")
finally:
    subprocess.run([server, "-k"], env=env, check=False)
    (a.results / "summary.json").write_text(json.dumps(results, indent=2) + "\n")
raise SystemExit(0 if all(x["assertions_passed"] for x in results) else 1)

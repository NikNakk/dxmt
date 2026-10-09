#!/usr/bin/env python3
"""Run isolated Wine 11.19 prerequisites and fixtures; retain every result."""
import argparse
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument("workspace", type=Path)
p.add_argument("smoke", type=Path)
p.add_argument("fixture", type=Path)
a = p.parse_args()
root = a.workspace.resolve() / "experimental"
logs = root / "logs"
logs.mkdir(exist_ok=True)
env = {k: v for k, v in os.environ.items() if not k.startswith(("WINE", "DXMT", "MWXR", "XR_"))}
env.update(WINEDEBUG="-all", DXMT_REMOTE_LAYER_DEBUG="1")
tests = [("wineboot", "", ["wineboot", "-u"], 180),
         ("engine-smoke", "", [str(a.smoke.resolve())], 90),
         ("ntdll-sync", "", [str(root / "build-11.19/dlls/ntdll/tests/x86_64-windows/ntdll_test.exe"), "sync"], 180),
         ("bcrypt", "", [str(root / "build-11.19/dlls/bcrypt/tests/x86_64-windows/bcrypt_test.exe"), "bcrypt"], 180),
         ("dxmt-smoke", "-dxmt", [str(a.smoke.resolve()), "--openxr-loader"], 90)]
tests += [("presentation-" + mode, "-dxmt", [str(a.fixture.resolve()), mode], 90)
          for mode in ["same", "root", "owned-child", "siblings", "terminate"]]
results = []
for name, suffix, args, timeout in tests:
    # Unit tests get their own prefix, apart from later Steam/graphics testing.
    case_env = dict(env, WINE_1119_PREFIX=str(root / ("prefix-unit-tests" if name in ["ntdll-sync", "bcrypt"]
                                                     else "prefix-11.19" + suffix)))
    with (logs / (name + ".log")).open("w") as output:
        process = subprocess.Popen([str(root / "bin" / ("wine-11.19" + suffix)), *args], env=case_env,
                                   stdout=output, stderr=subprocess.STDOUT)
        try:
            exit_code = process.wait(timeout=timeout)
            timed_out = False
        except subprocess.TimeoutExpired:
            timed_out = True
            process.kill()
            exit_code = process.wait()
            # Target ONLY the timed-out test prefix, never another bottle.
            server_env = dict(case_env, WINEPREFIX=case_env["WINE_1119_PREFIX"])
            subprocess.run([str(root / ("runtime-11.19" + suffix) / "bin/wineserver"), "-k"],
                           env=server_env, timeout=10)
    result = {"test": name, "exit": exit_code, "timed_out": timed_out, "log": str(logs / (name + ".log"))}
    results.append(result)
    (logs / "readiness-results.json").write_text(json.dumps(results, indent=2) + "\n")
    print(json.dumps(result), flush=True)
raise SystemExit(any(r["exit"] or r["timed_out"] for r in results))

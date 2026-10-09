#!/usr/bin/env python3
"""Capture explicit sibling paint order, plus replacement/resize content.
Requires an isolated provider launcher and the compiled ScreenCaptureKit tool.
"""
import argparse
import csv
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument("launcher", type=Path, help="isolated wine-dxmt-metal launcher")
p.add_argument("executable", type=Path)
p.add_argument("capture_tool", type=Path)
p.add_argument("results", type=Path)
a = p.parse_args()
a.results.mkdir(parents=True, exist_ok=True)
results = []
for fixture, count, expected in [("same-siblings-back", 20, "green"),
                                ("same-siblings-front", 20, "red"),
                                ("same-siblings", 40, None)]:
    title = "Wine Metal capture " + fixture + " " + str(os.getpid())
    env = os.environ.copy()
    env.update(DXMT_CROSS_PROCESS_SWAPCHAIN="wine", DXMT_REMOTE_LAYER_DEBUG="1",
               DXMT_TEST_TITLE=title, WINEDEBUG="-all")
    # Temporary assertion; no privacy or persistent power settings are changed.
    awake = subprocess.Popen(["caffeinate", "-u", "-d", "-t", "60"],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    renderer = capture = None
    code = capture_code = -1
    try:
        with (a.results / (fixture + ".log")).open("w") as log, \
             (a.results / (fixture + ".csv")).open("w") as data, \
             (a.results / (fixture + ".capture.log")).open("w") as errors:
            renderer = subprocess.Popen([str(a.launcher.resolve()), str(a.executable.resolve()), fixture],
                                        env=env, stdout=log, stderr=log)
            capture = subprocess.Popen([str(a.capture_tool.resolve()), title,
                                        str(a.results / (fixture + "-images")), str(count)],
                                       stdout=data, stderr=errors)
            code = renderer.wait(timeout=50)
            capture_code = capture.wait(timeout=30)
    except subprocess.TimeoutExpired:
        pass
    finally:
        for process in (renderer, capture, awake):
            if process and process.poll() is None:
                process.kill()
                process.wait()
    rows = list(csv.DictReader((a.results / (fixture + ".csv")).open()))
    valid = [r for r in rows if r.get("valid_sample") == "true"]
    def color(r):
        red, green, blue = [float(r["mean_" + c]) for c in "rgb"]
        if red > 150 and green < 80: return "red"
        if red < 80 and green > 80 and blue < 100: return "green"
        return "other"
    colors = {name: sum(color(r) == name for r in valid) for name in ("red", "green", "other")}
    passed = code == 0 and capture_code == 0 and len(rows) == count and bool(valid)
    passed &= all(r["near_black"] != "true" and
                  min(float(r["mean_" + c]) for c in "rgb") <= 240 for r in valid)
    if expected:
        passed &= colors[expected] == len(valid) and len(valid) >= count // 2
    result = dict(fixture=fixture, code=code, capture_code=capture_code, samples=len(rows),
                  valid=len(valid), inconclusive=len(rows) - len(valid), colors=colors,
                  expected_color=expected, passed=passed)
    results.append(result)
    print(json.dumps(result), flush=True)
(a.results / "summary.json").write_text(json.dumps(results, indent=2) + "\n")
raise SystemExit(0 if all(r["passed"] for r in results) else 1)

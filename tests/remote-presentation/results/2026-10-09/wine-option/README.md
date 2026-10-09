# Optional Wine presentation measurements, 2026-10-09

Apple M5 / macOS 26.6.2, x86_64 DXMT and Wine under Rosetta. No primary engine
or production prefix was modified. The patch was applied to a separate 11.19
source copy, and modified win32u/winemac Unix modules rebuilt from cloned vanilla
build objects. Full Wine build/install was previously completed for vanilla;
the new patched full-build command was not rerun end to end.

The JSON suites contain API/assertion results, including deliberate unavailable
HRESULTs. Unpatched 11.19 and copied CrossOver 26.3 each passed all nine cases.
The final patched provider suite is recorded separately. MSync was disabled in
these isolated suites. This does not establish Steam navigation, XR sessions,
32-bit compatibility or performance equivalence.

Capture CSVs measure actual central RGB and coverage during resize/context
replacement. `same-final` is the **failing development implementation**, before
Wine's GDI flush was made aware of active Metal ownership: 20 white-content
frames among 31 valid samples, although all rendering API assertions passed.
`same-preserved` is the corrected root trial: 36 valid / 40 samples, zero white
or black valid samples. The final child and sibling trials use unique captions
and Wine RetinaMode=y: respectively 27 and 30 valid samples out of 40, zero
white/black valid samples. Inconclusive samples had geometry races. One trial
per scenario does not prove freedom from short flashes; capture intervals are
approximately 0.18–0.3 seconds. Whole-window captures include non-client borders,
so their dimensions alone do not establish exact client/Metal pixel agreement.

Root and child fixtures made 20 replacements in the corrected capture runs.
Sibling colors alternate in captures and both surfaces churn; the center metric
is not a formal paint-order/clipping assertion. Nested clipping, exact edge
analysis, per-monitor scale changes, mixed GDI overlays and HWND reuse remain
manual validation requirements.

Development also exposed a Wine client-surface function-table identity assertion
when a custom provider table was first introduced. That assertion was corrected
before these final tests. The initial obsolete suite's termination check sampled
its log before the surviving renderer finished; the runner now waits for the
renderer result. Those development failures are not counted as passing trials.

Full logs and PNGs remain in `/tmp/wine-option-tests`, `/tmp/wine-option-final-suite`,
`/tmp/wine-vanilla-auto-suite`, `/tmp/wine-crossover-auto-suite` on this host.
Recreate them with the documented harness rather than relying on temp retention.

The static bottom-order scene exposed another real failure: before Wine's
sibling-order helper, all 20 samples were red despite explicit HWND_BOTTOM.
After the helper, the same scene produced 20/20 green samples. An explicitly
asserted HWND_TOP scene produced 20/20 red samples. The final ordering harness
passed both static cases plus churn (31/40 valid, nine inconclusive, no blank
valid samples). Earlier front-control captures had not explicitly requested
HWND_TOP and are excluded from this ordering conclusion.

The initial 30 suite cases precede the final sibling-order helper. Post-helper
validation exercised the changed child/sibling code in real captures; root and
remote DXMT paths were unaffected by that helper. Final packaged binary hashes
are recorded separately.

Final packaged validation after first-submission locking was restricted to
establishment/retry passed Wine sibling churn and remote `auto` root. The
Wine run created and released 42 tokens, with at most three simultaneously
owned and none remaining. These are API token counts, not a count of AppKit
objects retained temporarily by Wine's asynchronous main-thread blocks.

# D3D11 timestamp query fixture

`timestamp_query.cpp` times copies (default) or 8192x8192 render-target
clears (`TSQ_CLEARS=1`) between two `D3D11_QUERY_TIMESTAMP` queries, with
and without unflushed work queued before the first query, and compares the
difference with the CPU-observed completion of an event query issued after
the work. Build it with mingw-w64 and run it under Wine with the DXMT build
to test, staged with `tests/remote-presentation/stage_runtime.py`:

    x86_64-w64-mingw32-g++ -std=c++17 -O1 -static -o timestamp_query.exe timestamp_query.cpp -ld3d11 -ldxgi

Results on Apple M5, 2026-10-14, clears with four clears queued before the
first query and ten measured: the command-buffer `gpuEndTime` readback
reported 2.885 ms against 8.805 ms flush-to-event (FAIL); the sample-buffer
readback reported 6.582 ms against 8.091 ms (pass). All other cases passed
with both readbacks. The flush-to-event time includes the queued work and
completion latency, so it bounds rather than equals the measured interval.
